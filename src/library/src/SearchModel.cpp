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
      m_results.setEntries({});
      setLoading(false);
      setError({});
      return;
    }
    m_delay.start();
  }

  void SearchModel::follow(LibraryUpdates& updates) {
    connect(&updates, &LibraryUpdates::userDataChanged, &m_results, &ItemListModel::applyUserData);
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
    m_results.setEntries(std::move(entries));
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
