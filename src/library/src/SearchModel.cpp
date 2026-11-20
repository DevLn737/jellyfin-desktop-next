#include <jfd/library/SearchModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ErrorText.h>
#include <jfd/library/CardImages.h>
#include <jfd/library/LibraryUpdates.h>

namespace jfd::library {

  SearchModel::SearchModel(api::JellyfinClient& client, Options options, QObject* parent)
      : QObject(parent), m_client(client), m_options(options) {
    m_delay.setSingleShot(true);
    m_delay.setInterval(m_options.delayMs);
    connect(&m_delay, &QTimer::timeout, this, &SearchModel::submit);
  }

  void SearchModel::setText(const QString& text) {
    if (m_text == text) return;
    m_text = text;
    Q_EMIT textChanged();
    if (m_text.trimmed().isEmpty()) {
      // Clearing is immediate, and any answer still on its way is dropped.
      m_delay.stop();
      ++m_serial;
      m_sent.clear();
      m_items.reset();
      m_people.reset();
      setAll({});
      setLoading(false);
      setError({});
      return;
    }
    m_delay.start();
  }

  void SearchModel::follow(LibraryUpdates& updates) {
    connect(&updates, &LibraryUpdates::userDataChanged, &m_results, &ItemListModel::applyUserData);
    // The entries of other kinds keep it too, for when their tab shows.
    connect(&updates, &LibraryUpdates::userDataChanged, this, [this](const api::UserItemData& data) {
      for (ItemListModel::Entry& entry : m_all) {
        if (entry.item.id != data.itemId) continue;
        entry.item.userData = data;
        entry.item.userData.itemId.clear();
      }
    });
  }

  void SearchModel::submit() {
    m_delay.stop();
    const QString term = m_text.trimmed();
    if (term.isEmpty() || term == m_sent) return;
    m_sent = term;
    const quint64 serial = ++m_serial;
    setLoading(true);
    setError({});
    m_items.reset();
    m_people.reset();
    api::ItemQuery query;
    query.searchTerm = term;
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series"),
                              QStringLiteral("Episode"), QStringLiteral("Video"),
                              QStringLiteral("MusicAlbum"), QStringLiteral("MusicArtist"),
                              QStringLiteral("Playlist"), QStringLiteral("Audio")};
    query.limit = m_options.limit;
    m_client.items(query, this, [this, serial](api::Result<api::ItemPage> result) {
      if (serial != m_serial) return;
      m_items = std::move(result);
      finish();
    });
    if (m_options.peopleLimit <= 0) {
      m_people = QList<api::Item>();
      return;
    }
    m_client.persons(term, m_options.peopleLimit, this,
                     [this, serial](api::Result<api::ItemPage> result) {
                       if (serial != m_serial) return;
                       // A server without the people search still finds the rest.
                       m_people = result ? result->items : QList<api::Item>();
                       finish();
                     });
  }

  void SearchModel::finish() {
    if (!m_items || !m_people) return;
    setLoading(false);
    const api::Result<api::ItemPage> items = std::move(*m_items);
    QList<api::Item> people = std::move(*m_people);
    m_items.reset();
    m_people.reset();
    if (!items) {
      // The text can be sent again after a failure.
      m_sent.clear();
      setError(library::errorText(items.error()));
      return;
    }
    const QSize pixels =
        cardImageSize(CardShape::Poster, m_options.cardWidth, m_options.devicePixelRatio);
    QList<ItemListModel::Entry> entries;
    for (const api::Item& item : items->items)
      entries.append({item, cardImageUrl(m_client, item, CardShape::Poster, pixels)});
    for (api::Item& person : people) {
      // The card picks the person page by type.
      if (person.type.isEmpty()) person.type = QStringLiteral("Person");
      entries.append({person, cardImageUrl(m_client, person, CardShape::Poster, pixels)});
    }
    setAll(std::move(entries));
  }

  QString SearchModel::kindOf(const QString& type) {
    if (type == QLatin1String("Movie") || type == QLatin1String("Video")) return QStringLiteral("movies");
    if (type == QLatin1String("Series")) return QStringLiteral("series");
    if (type == QLatin1String("Episode")) return QStringLiteral("episodes");
    if (type == QLatin1String("Person")) return QStringLiteral("people");
    if (type == QLatin1String("MusicAlbum") || type == QLatin1String("MusicArtist") ||
        type == QLatin1String("Playlist") || type == QLatin1String("Audio"))
      return QStringLiteral("music");
    return {};
  }

  void SearchModel::setAll(QList<ItemListModel::Entry> entries) {
    m_all = std::move(entries);
    QVariantMap counts;
    counts.insert(QStringLiteral("all"), static_cast<int>(m_all.size()));
    for (const ItemListModel::Entry& entry : m_all) {
      const QString kind = kindOf(entry.item.type);
      if (!kind.isEmpty()) counts.insert(kind, counts.value(kind).toInt() + 1);
    }
    if (counts != m_counts) {
      m_counts = counts;
      Q_EMIT countsChanged();
    }
    showKind();
  }

  void SearchModel::setKind(const QString& kind) {
    const QString value = kind.isEmpty() ? QStringLiteral("all") : kind;
    if (value == m_kind) return;
    m_kind = value;
    Q_EMIT kindChanged();
    showKind();
  }

  void SearchModel::showKind() {
    if (m_kind == QLatin1String("all")) {
      m_results.setEntries(m_all);
      return;
    }
    QList<ItemListModel::Entry> shown;
    for (const ItemListModel::Entry& entry : m_all)
      if (kindOf(entry.item.type) == m_kind) shown.append(entry);
    m_results.setEntries(std::move(shown));
  }

  void SearchModel::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void SearchModel::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

} // namespace jfd::library
