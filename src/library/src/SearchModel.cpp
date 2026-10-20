#include <jfd/library/SearchModel.h>

#include <jfd/api/JellyfinClient.h>
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
    api::ItemQuery query;
    query.searchTerm = term;
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series"),
                              QStringLiteral("Episode"), QStringLiteral("Video")};
    query.limit = m_options.limit;
    m_client.items(query, this, [this, serial](api::Result<api::ItemPage> result) {
      if (serial != m_serial) return;
      setLoading(false);
      if (!result) {
        // The text can be sent again after a failure.
        m_sent.clear();
        setError(result.error().message);
        return;
      }
      const QSize pixels =
          cardImageSize(CardShape::Poster, m_options.cardWidth, m_options.devicePixelRatio);
      QList<ItemListModel::Entry> entries;
      for (const api::Item& item : result->items)
        entries.append({item, cardImageUrl(m_client, item, CardShape::Poster, pixels)});
      m_results.setEntries(std::move(entries));
    });
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
