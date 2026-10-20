#include <jfd/library/ItemBrowser.h>

#include <jfd/api/JellyfinClient.h>

namespace jfd::library {

  namespace {
    constexpr int ImageWidth = 400;
  } // namespace

  ItemBrowser::ItemBrowser(api::JellyfinClient& client, QObject* parent)
      : QObject(parent), m_client(client) {}

  quint64 ItemBrowser::beginRequest(const QString& title) {
    if (m_title != title) {
      m_title = title;
      Q_EMIT titleChanged();
    }
    setErrorText({});
    setLoading(true);
    return ++m_serial;
  }

  void ItemBrowser::finish(quint64 serial, api::Result<api::ItemPage> result) {
    if (serial != m_serial) return;
    setLoading(false);
    if (!result) {
      setErrorText(result.error().message);
      return;
    }
    QList<ItemListModel::Entry> entries;
    entries.reserve(result->items.size());
    for (const api::Item& item : result->items)
      entries.append({item, m_client.primaryImageUrl(item, ImageWidth)});
    m_model.setEntries(std::move(entries));
  }

  void ItemBrowser::search(const QString& term) {
    const QString trimmed = term.trimmed();
    const quint64 serial = beginRequest(trimmed);
    if (trimmed.isEmpty()) {
      setLoading(false);
      m_model.setEntries({});
      return;
    }
    api::ItemQuery query;
    query.searchTerm = trimmed;
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series"),
                              QStringLiteral("Episode")};
    query.limit = 100;
    m_client.items(query, this,
                   [this, serial](api::Result<api::ItemPage> r) { finish(serial, std::move(r)); });
  }

  void ItemBrowser::showSeasons(const QString& seriesId, const QString& seriesName) {
    const quint64 serial = beginRequest(seriesName);
    m_client.seasons(seriesId, this, [this, serial](api::Result<api::ItemPage> r) {
      finish(serial, std::move(r));
    });
  }

  void ItemBrowser::showEpisodes(const QString& seriesId, const QString& seasonId,
                                 const QString& seasonName) {
    const quint64 serial = beginRequest(seasonName);
    m_client.episodes(seriesId, seasonId, this, [this, serial](api::Result<api::ItemPage> r) {
      finish(serial, std::move(r));
    });
  }

  void ItemBrowser::activate(int row) {
    const auto* e = m_model.entry(row);
    if (!e) return;
    const api::Item& item = e->item;
    if (item.type == QLatin1String("Series"))
      showSeasons(item.id, item.name);
    else if (item.type == QLatin1String("Season"))
      showEpisodes(item.seriesId, item.id,
                   item.seriesName.isEmpty() ? item.name
                                             : item.seriesName + QStringLiteral(" — ") + item.name);
    else
      Q_EMIT playRequested(item.id);
  }

  void ItemBrowser::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void ItemBrowser::setErrorText(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

} // namespace jfd::library
