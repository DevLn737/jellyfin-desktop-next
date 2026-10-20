#include <jfd/library/HomeModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/LibraryUpdates.h>

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
  } // namespace

  HomeModel::HomeModel(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_options(options) {
    m_reloadSoon.setSingleShot(true);
    m_reloadSoon.setInterval(m_options.updateDelayMs);
    connect(&m_reloadSoon, &QTimer::timeout, this, &HomeModel::reload);
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
      return QVariant::fromValue(row->items.get());
    case CardShapeRole:
      return shapeName(row->shape);
    default:
      return {};
    }
  }

  QHash<int, QByteArray> HomeModel::roleNames() const {
    return {{TitleRole, "title"},
            {KindRole, "kind"},
            {LibraryIdRole, "libraryId"},
            {ItemsRole, "items"},
            {CardShapeRole, "cardShape"}};
  }

  ItemListModel* HomeModel::rowItems(int row) const {
    return row >= 0 && row < count() ? m_visible[row]->items.get() : nullptr;
  }

  QString HomeModel::rowKind(int row) const {
    return row >= 0 && row < count() ? m_visible[row]->kind : QString();
  }

  void HomeModel::follow(LibraryUpdates& updates) {
    connect(&updates, &LibraryUpdates::userDataChanged, this,
            [this](const api::UserItemData& data) {
              for (const auto& row : m_rows)
                row->items->applyUserData(data);
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
      QString kind;
      QString libraryId;
      QString title;
      CardShape shape;
    };
    QList<Spec> specs{{QStringLiteral("resume"), {}, tr("Continue watching"), CardShape::Thumb},
                      {QStringLiteral("nextUp"), {}, tr("Next up"), CardShape::Thumb}};
    for (int i = 0; i < m_libraries.count(); ++i) {
      const api::Item& view = m_libraries.entry(i)->item;
      if (hasLatestRow(view.collectionType))
        specs.append({QStringLiteral("latest"), view.id, tr("Latest in %1").arg(view.name),
                      CardShape::Poster});
    }

    // Rows that stay keep their items until the new answers arrive.
    std::vector<std::unique_ptr<Row>> rows;
    for (const Spec& spec : specs) {
      auto existing = std::find_if(m_rows.begin(), m_rows.end(), [&](const auto& row) {
        return row && row->kind == spec.kind && row->libraryId == spec.libraryId;
      });
      std::unique_ptr<Row> row;
      if (existing != m_rows.end()) {
        row = std::move(*existing);
      } else {
        row = std::make_unique<Row>();
        row->kind = spec.kind;
        row->libraryId = spec.libraryId;
        row->shape = spec.shape;
        row->items = std::make_unique<ItemListModel>();
      }
      row->title = spec.title;
      rows.push_back(std::move(row));
    }
    // Rows of libraries that went away leave the view before they are freed.
    for (int at = static_cast<int>(m_visible.size()) - 1; at >= 0; --at) {
      const bool kept = std::any_of(rows.begin(), rows.end(),
                                    [&](const auto& row) { return row.get() == m_visible[at]; });
      if (kept) continue;
      beginRemoveRows({}, at, at);
      m_visible.removeAt(at);
      endRemoveRows();
      Q_EMIT countChanged();
    }
    m_rows = std::move(rows);

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

  void HomeModel::fill(quint64 generation, Row* row, api::Result<api::ItemPage> result) {
    if (generation != m_generation) return;
    if (result) {
      row->items->setEntries(entries(result->items, row->shape));
      row->loaded = true;
      updateVisible();
    } else {
      // A failed row keeps what it showed.
      setError(result.error().message);
    }
    requestDone();
  }

  void HomeModel::updateVisible() {
    // Insert and remove single rows so the view keeps its scroll position.
    int at = 0;
    for (const auto& owned : m_rows) {
      Row* row = owned.get();
      const bool show = row->loaded && row->items->count() > 0;
      const bool shown = at < m_visible.size() && m_visible[at] == row;
      if (show && !shown) {
        beginInsertRows({}, at, at);
        m_visible.insert(at, row);
        endInsertRows();
        Q_EMIT countChanged();
      } else if (!show && shown) {
        beginRemoveRows({}, at, at);
        m_visible.removeAt(at);
        endRemoveRows();
        Q_EMIT countChanged();
        continue;
      } else if (show) {
        const QModelIndex i = index(at);
        Q_EMIT dataChanged(i, i);
      }
      if (show) ++at;
    }
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
