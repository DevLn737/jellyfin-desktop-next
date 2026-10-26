#include <jfd/library/HomeModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/LibraryUpdates.h>
#include <jfd/library/RecommendationRows.h>

#include <algorithm>

namespace jfd::library {

  namespace {
    // Library types with a "latest" row; music, books, playlists, collections
    // and live TV are left to their own screens, as in Jellyfin Web's video
    // sections.
    bool hasLatestRow(const QString& collectionType) {
      return collectionType.isEmpty() || collectionType == QLatin1String("movies") ||
             collectionType == QLatin1String("tvshows") ||
             collectionType == QLatin1String("homevideos") ||
             collectionType == QLatin1String("musicvideos");
    }

    QString shapeName(CardShape shape) {
      return shape == CardShape::Thumb ? QStringLiteral("thumb") : QStringLiteral("poster");
    }

    CardShape cardShapeOf(const QString& name) {
      return name == QLatin1String("thumb") ? CardShape::Thumb
             : (name == QLatin1String("square") || name == QLatin1String("circle")) ? CardShape::Square
                                                                                      : CardShape::Poster;
    }
  } // namespace

  HomeModel::HomeModel(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_options(options) {
    m_reloadSoon.setSingleShot(true);
    m_reloadSoon.setInterval(m_options.updateDelayMs);
    connect(&m_reloadSoon, &QTimer::timeout, this, &HomeModel::reload);
    connect(&m_sections, &HomeSections::changed, this, [this] {
      syncSections();
      Q_EMIT sectionListChanged();
    });
    if (m_options.recommendations) {
      RecommendationRows::Options rows;
      rows.cardWidth = m_options.cardWidth;
      rows.devicePixelRatio = m_options.devicePixelRatio;
      rows.refreshMs = m_options.recommendationRefreshMs;
      rows.timeoutMs = m_options.recommendationTimeoutMs;
      m_recommendations = new RecommendationRows(m_client, m_sections, rows, this);
    }
  }

  void HomeModel::reloadSoon() {
    if (!m_reloadSoon.isActive()) m_reloadSoon.start();
  }

  HomeModel::~HomeModel() = default;

  int HomeModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant HomeModel::data(const QModelIndex& index, int role) const {
    if (index.parent().isValid() || index.row() < 0 || index.row() >= count()) return {};
    const Row* row = m_visible[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
      return row->title;
    case KindRole:
      return row->kind;
    case LibraryIdRole:
      return row->libraryId;
    case ItemsRole:
      return QVariant::fromValue(static_cast<QObject*>(row->model.data()));
    case CardShapeRole:
      return row->shape;
    case SectionIdRole:
      return row->id;
    default:
      return {};
    }
  }

  QHash<int, QByteArray> HomeModel::roleNames() const {
    return {{TitleRole, "title"},
            {KindRole, "kind"},
            {LibraryIdRole, "libraryId"},
            {ItemsRole, "items"},
            {CardShapeRole, "cardShape"},
            {SectionIdRole, "sectionId"}};
  }

  ItemListModel* HomeModel::rowItems(int row) const {
    return row >= 0 && row < count() ? qobject_cast<ItemListModel*>(m_visible[row]->model.data()) : nullptr;
  }

  QString HomeModel::rowKind(int row) const {
    return row >= 0 && row < count() ? m_visible[row]->kind : QString();
  }

  void HomeModel::follow(LibraryUpdates& updates) {
    connect(&updates, &LibraryUpdates::userDataChanged, this,
            [this](const api::UserItemData& data) {
              for (const auto& row : m_rows)
                row->items->applyUserData(data);
              if (m_recommendations != nullptr) {
                // Recommended cards show watched state too.
                for (const auto& row : m_external) {
                  if (auto* items = qobject_cast<ItemListModel*>(row->model.data())) items->applyUserData(data);
                }
              }
              // Watching something changes what "Continue watching" and
              // "Next up" hold. Progress reports during playback arrive every
              // few seconds, so reloads are collected.
              if (data.played || data.playbackPositionTicks > 0) reloadSoon();
            });
    connect(&updates, &LibraryUpdates::libraryChanged, this, &HomeModel::reloadSoon);
    connect(&updates, &LibraryUpdates::resynchronize, this, &HomeModel::reloadSoon);
  }

  QList<ItemListModel::Entry> HomeModel::entries(const QList<api::Item>& items,
                                                 CardShape shape) const {
    const QSize pixels = cardImageSize(shape, m_options.cardWidth, m_options.devicePixelRatio);
    QList<ItemListModel::Entry> out;
    out.reserve(items.size());
    for (const api::Item& item : items)
      out.append({item, cardImageUrl(m_client, item, shape, pixels)});
    return out;
  }

  void HomeModel::reload() {
    m_reloadSoon.stop();
    // Other modules' rows and the recommendations (which keep their answers
    // for a few minutes).
    for (const auto& row : m_external) {
      if (row->reload) row->reload();
    }
    if (m_recommendations != nullptr) m_recommendations->refresh();
    const quint64 generation = ++m_generation;
    const bool wasLoading = loading();
    m_pending = 1;
    if (!wasLoading) Q_EMIT loadingChanged();
    setError({});
    m_client.views(this, [this, generation](api::Result<api::ItemPage> result) {
      if (generation != m_generation) return;
      if (!result) {
        setError(result.error().message);
        requestDone();
        return;
      }
      const QSize pixels =
          cardImageSize(CardShape::Thumb, m_options.cardWidth, m_options.devicePixelRatio);
      QList<ItemListModel::Entry> libraries;
      for (const api::Item& view : result->items)
        libraries.append({view, cardImageUrl(m_client, view, CardShape::Thumb, pixels)});
      m_libraries.setEntries(std::move(libraries));
      loadRows(generation);
      requestDone();
    });
  }

  void HomeModel::loadRows(quint64 generation) {
    struct Spec {
      QString id;
      QString kind;
      QString libraryId;
      QString title;
      CardShape shape;
      int order;
    };
    QList<Spec> specs{{QStringLiteral("resume"), QStringLiteral("resume"), {}, tr("Continue watching"), CardShape::Thumb, 10},
                      {QStringLiteral("nextUp"), QStringLiteral("nextUp"), {}, tr("Next up"), CardShape::Thumb, 20}};
    int latestOrder = 100;
    for (int i = 0; i < m_libraries.count(); ++i) {
      const api::Item& view = m_libraries.entry(i)->item;
      if (hasLatestRow(view.collectionType)) {
        specs.append({QStringLiteral("latest/") + view.id, QStringLiteral("latest"), view.id,
                      tr("Latest in %1").arg(view.name), CardShape::Poster, latestOrder});
        latestOrder += 10;
      }
    }

    // Rows that stay keep their items until the new answers arrive.
    std::vector<std::unique_ptr<Row>> rows;
    for (const Spec& spec : specs) {
      auto existing = std::find_if(m_rows.begin(), m_rows.end(), [&](const auto& row) {
        return row && row->id == spec.id;
      });
      std::unique_ptr<Row> row;
      if (existing != m_rows.end()) {
        row = std::move(*existing);
      } else {
        row = std::make_unique<Row>();
        row->id = spec.id;
        row->kind = spec.kind;
        row->libraryId = spec.libraryId;
        row->shape = shapeName(spec.shape);
        row->items = std::make_unique<ItemListModel>();
        row->model = row->items.get();
      }
      row->title = spec.title;
      rows.push_back(std::move(row));
    }
    // Rows of libraries that went away leave the view before they are freed.
    for (int at = static_cast<int>(m_visible.size()) - 1; at >= 0; --at) {
      const bool kept = std::any_of(rows.begin(), rows.end(),
                                    [&](const auto& row) { return row.get() == m_visible[at]; });
      if (kept || !m_visible[at]->builtin) continue;
      beginRemoveRows({}, at, at);
      m_visible.removeAt(at);
      endRemoveRows();
      Q_EMIT countChanged();
    }
    // Their sections go too; the rows being kept are registered again with
    // the current titles. (Sections changing makes syncSections look at
    // the rows, so the new list is in place first.)
    QStringList goneIds;
    for (const auto& old : m_rows) {
      if (old && !std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row && row->id == old->id; }))
        goneIds.append(old->id);
    }
    m_rows = std::move(rows);
    m_syncing = true;
    for (const QString& id : std::as_const(goneIds)) m_sections.remove(id);
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
      const Row& row = *m_rows[i];
      int order = 0;
      for (const Spec& spec : specs) {
        if (spec.id == row.id) order = spec.order;
      }
      addBuiltinSection(row, order);
    }
    m_syncing = false;
    Q_EMIT sectionListChanged();

    m_pending += static_cast<int>(m_rows.size());
    for (const auto& owned : m_rows) {
      Row* row = owned.get();
      auto done = [this, generation, row](api::Result<api::ItemPage> r) {
        fill(generation, row, std::move(r));
      };
      if (row->kind == QLatin1String("resume"))
        m_client.resumeItems(m_options.resumeLimit, {}, this, done);
      else if (row->kind == QLatin1String("nextUp"))
        m_client.nextUp(m_options.nextUpLimit, {}, this, done);
      else
        m_client.latestItems(row->libraryId, m_options.latestLimit, this, done);
    }
  }

  void HomeModel::addBuiltinSection(const Row& row, int order) {
    HomeSection section;
    section.id = row.id;
    section.title = row.title;
    section.cardShape = row.shape;
    section.defaultOrder = order;
    section.model = row.model;
    m_sections.add(std::move(section));
  }

  void HomeModel::fill(quint64 generation, Row* row, api::Result<api::ItemPage> result) {
    if (generation != m_generation) return;
    if (result) {
      row->items->setEntries(entries(result->items, cardShapeOf(row->shape)));
      row->loaded = true;
      updateVisible();
    } else {
      // A failed row keeps what it showed.
      setError(result.error().message);
    }
    requestDone();
  }

  HomeModel::Row* HomeModel::findRow(const QString& id) const {
    for (const auto& row : m_rows) {
      if (row->id == id) return row.get();
    }
    for (const auto& row : m_external) {
      if (row->id == id) return row.get();
    }
    return nullptr;
  }

  bool HomeModel::shown(const Row& row) const {
    return row.loaded && row.model != nullptr && row.model->rowCount() > 0 &&
           !m_arrangement.hidden.contains(row.id);
  }

  void HomeModel::scheduleUpdate() {
    if (m_updateQueued) return;
    m_updateQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
          m_updateQueued = false;
          updateVisible();
        },
        Qt::QueuedConnection);
  }

  // Other modules' sections become rows of their own (and leave when the
  // module takes them back).
  void HomeModel::syncSections() {
    if (m_syncing) return;
    m_syncing = true;
    QStringList wanted;
    for (const HomeSection* section : m_sections.byDefaultOrder()) {
      const bool builtin = std::any_of(m_rows.begin(), m_rows.end(),
                                       [&](const auto& row) { return row->id == section->id; });
      if (builtin) continue;
      wanted.append(section->id);
      Row* row = nullptr;
      for (const auto& existing : m_external) {
        if (existing->id == section->id) row = existing.get();
      }
      if (row == nullptr) {
        m_external.push_back(std::make_unique<Row>());
        row = m_external.back().get();
        row->id = section->id;
        row->kind = section->id.startsWith(QLatin1String("recommend/")) ? QStringLiteral("recommend")
                                                                          : QStringLiteral("section");
        row->builtin = false;
        row->loaded = true;
      }
      row->title = section->title;
      row->shape = section->cardShape;
      row->reload = section->reload;
      if (row->model != section->model) {
        for (const auto& connection : std::as_const(row->connections)) disconnect(connection);
        row->connections.clear();
        row->model = section->model;
        if (row->model != nullptr) {
          const QAbstractItemModel* model = row->model.data();
          const auto update = [this] { scheduleUpdate(); };
          row->connections.append(connect(model, &QAbstractItemModel::rowsInserted, this, update));
          row->connections.append(connect(model, &QAbstractItemModel::rowsRemoved, this, update));
          row->connections.append(connect(model, &QAbstractItemModel::modelReset, this, update));
        }
      }
    }
    // Take the rows of vanished sections off the screen, then drop them.
    for (const auto& row : m_external) {
      if (!wanted.contains(row->id)) row->model = nullptr;
    }
    m_syncing = false;
    updateVisible();
    m_external.erase(std::remove_if(m_external.begin(), m_external.end(),
                                    [&](const auto& row) { return !wanted.contains(row->id); }),
                     m_external.end());
  }

  void HomeModel::updateVisible() {
    // The rows to show, in the user's order. Insert, remove and move single
    // rows so the view keeps its scroll position.
    QList<Row*> target;
    for (const QString& id : m_sections.displayOrder(m_arrangement.order)) {
      Row* row = findRow(id);
      if (row != nullptr && shown(*row)) target.append(row);
    }
    for (int at = static_cast<int>(m_visible.size()) - 1; at >= 0; --at) {
      if (target.contains(m_visible[at])) continue;
      beginRemoveRows({}, at, at);
      m_visible.removeAt(at);
      endRemoveRows();
      Q_EMIT countChanged();
    }
    for (int to = 0; to < target.size(); ++to) {
      Row* row = target[to];
      if (to < m_visible.size() && m_visible[to] == row) {
        const QModelIndex i = index(to);
        Q_EMIT dataChanged(i, i);
        continue;
      }
      const int from = static_cast<int>(m_visible.indexOf(row));
      if (from >= 0) {
        beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
        m_visible.move(from, to);
        endMoveRows();
      } else {
        beginInsertRows({}, to, to);
        m_visible.insert(to, row);
        endInsertRows();
        Q_EMIT countChanged();
      }
    }
    Q_EMIT sectionListChanged();
  }

  void HomeModel::setArrangement(const HomeArrangement& arrangement) {
    if (arrangement == m_arrangement) return;
    m_arrangement = arrangement;
    updateVisible();
  }

  QVariantList HomeModel::sectionList() const {
    QVariantList list;
    for (const QString& id : m_sections.displayOrder(m_arrangement.order)) {
      const HomeSection* section = m_sections.find(id);
      if (section == nullptr) continue;
      list.append(QVariantMap{{QStringLiteral("id"), id},
                              {QStringLiteral("title"), section->title},
                              {QStringLiteral("visible"), !m_arrangement.hidden.contains(id)}});
    }
    return list;
  }

  void HomeModel::moveSection(int from, int to) {
    QStringList order = m_sections.displayOrder(m_arrangement.order);
    if (from < 0 || from >= order.size() || to < 0 || to >= order.size() || from == to) return;
    order.move(from, to);
    m_arrangement.order = order;
    updateVisible();
    Q_EMIT arrangementChanged();
  }

  void HomeModel::setSectionVisible(const QString& id, bool visible) {
    if (!m_sections.contains(id)) return;
    // The whole order is kept once the user has touched it, so a section
    // that appears later does not jump in front of what they arranged.
    if (m_arrangement.order.isEmpty()) m_arrangement.order = m_sections.displayOrder({});
    if (visible) {
      m_arrangement.hidden.removeAll(id);
    } else if (!m_arrangement.hidden.contains(id)) {
      m_arrangement.hidden.append(id);
    }
    updateVisible();
    Q_EMIT arrangementChanged();
  }

  void HomeModel::resetArrangement() {
    if (m_arrangement == HomeArrangement{}) return;
    m_arrangement = {};
    updateVisible();
    Q_EMIT arrangementChanged();
  }

  void HomeModel::resetSession() {
    ++m_generation;
    m_reloadSoon.stop();
    if (m_recommendations != nullptr) m_recommendations->clear();
    // Leave the screen empty until the next user's rows arrive.
    beginResetModel();
    m_visible.clear();
    endResetModel();
    Q_EMIT countChanged();
    m_syncing = true;
    for (const auto& row : m_rows) m_sections.remove(row->id);
    m_rows.clear();
    for (const auto& row : m_external) row->model = nullptr;
    m_syncing = false;
    m_external.clear();
    m_libraries.setEntries({});
    if (m_pending != 0) {
      m_pending = 0;
      Q_EMIT loadingChanged();
    }
    Q_EMIT sectionListChanged();
  }

  void HomeModel::requestDone() {
    if (m_pending == 0) return;
    if (--m_pending == 0) Q_EMIT loadingChanged();
  }

  void HomeModel::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

} // namespace jfd::library
