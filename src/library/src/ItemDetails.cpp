#include <jfd/library/ItemDetails.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/CardImages.h>
#include <jfd/library/LibraryUpdates.h>

namespace jfd::library {

  namespace {
    bool is(const api::Item& item, const char* type) {
      return item.type == QLatin1String(type);
    }

    bool isPlayable(const api::Item& item) {
      return !item.isFolder && !is(item, "Series") && !is(item, "Season");
    }
  } // namespace

  ItemDetails::ItemDetails(api::JellyfinClient& client, Options options, QObject* parent)
      : QObject(parent), m_client(client), m_options(options) {}

  void ItemDetails::setItemId(const QString& id) {
    if (m_itemId == id) return;
    m_itemId = id;
    Q_EMIT itemIdChanged();
    reload();
  }

  void ItemDetails::setUpdates(LibraryUpdates* updates) {
    if (m_updates) m_updates->disconnect(this);
    m_updates = updates;
    if (!updates) return;
    connect(updates, &LibraryUpdates::userDataChanged, this, &ItemDetails::applyUserData);
    connect(updates, &LibraryUpdates::resynchronize, this, &ItemDetails::reload);
  }

  // --- Properties ----------------------------------------------------------

  QString ItemDetails::name() const {
    return m_item ? m_item->name : QString();
  }
  QString ItemDetails::type() const {
    return m_item ? m_item->type : QString();
  }
  QString ItemDetails::overview() const {
    return m_item ? m_item->overview : QString();
  }
  QVariant ItemDetails::year() const {
    return m_item && m_item->productionYear ? QVariant(*m_item->productionYear) : QVariant();
  }
  QStringList ItemDetails::genres() const {
    return m_item ? m_item->genres : QStringList();
  }
  QVariant ItemDetails::communityRating() const {
    return m_item && m_item->communityRating ? QVariant(*m_item->communityRating) : QVariant();
  }
  QString ItemDetails::officialRating() const {
    return m_item ? m_item->officialRating : QString();
  }
  qint64 ItemDetails::runTimeTicks() const {
    return m_item ? m_item->runTimeTicks.value_or(0) : 0;
  }
  QString ItemDetails::seriesId() const {
    return m_item ? m_item->seriesId : QString();
  }
  QString ItemDetails::seriesName() const {
    return m_item ? m_item->seriesName : QString();
  }
  QString ItemDetails::subtitle() const {
    if (!m_item || !is(*m_item, "Episode") || !m_item->indexNumber) return {};
    if (m_item->parentIndexNumber)
      return QStringLiteral("S%1:E%2").arg(*m_item->parentIndexNumber).arg(*m_item->indexNumber);
    return QStringLiteral("E%1").arg(*m_item->indexNumber);
  }
  bool ItemDetails::played() const {
    return m_item && m_item->userData.played;
  }
  bool ItemDetails::favorite() const {
    return m_item && m_item->userData.isFavorite;
  }
  qint64 ItemDetails::playbackPositionTicks() const {
    return m_item ? m_item->userData.playbackPositionTicks : 0;
  }
  double ItemDetails::progress() const {
    return m_item ? progressOf(*m_item) : 0.0;
  }
  qint64 ItemDetails::resumePositionTicks() const {
    return m_target ? m_target->userData.playbackPositionTicks : 0;
  }

  // --- Loading -------------------------------------------------------------

  void ItemDetails::begin() {
    if (m_pending++ == 0) Q_EMIT loadingChanged();
  }

  void ItemDetails::done() {
    if (m_pending > 0 && --m_pending == 0) Q_EMIT loadingChanged();
  }

  void ItemDetails::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

  QList<ItemListModel::Entry> ItemDetails::entries(const QList<api::Item>& items,
                                                   bool episodes) const {
    const CardShape shape = episodes ? CardShape::Thumb : CardShape::Poster;
    const QSize pixels =
        cardImageSize(shape, episodes ? m_options.episodeWidth : m_options.posterWidth,
                      m_options.devicePixelRatio);
    QList<ItemListModel::Entry> out;
    for (const api::Item& item : items)
      out.append({item, cardImageUrl(m_client, item, shape, pixels)});
    return out;
  }

  void ItemDetails::reload() {
    const quint64 generation = ++m_generation;
    const bool wasLoading = loading();
    m_pending = 0;
    if (wasLoading) Q_EMIT loadingChanged();
    setError({});
    if (m_itemId.isEmpty()) return;
    // A different item clears the page; the same one keeps it until the
    // new answer arrives.
    if (m_item && m_item->id != m_itemId) {
      m_item.reset();
      m_seasons.setEntries({});
      m_episodes.setEntries({});
      m_similar.setEntries({});
      m_seasonId.clear();
      setTarget(std::nullopt);
      m_posterUrl = m_backdropUrl = m_logoUrl = QUrl();
      Q_EMIT itemChanged();
      Q_EMIT userDataChanged();
      Q_EMIT seasonIdChanged();
    }
    begin();
    m_client.itemDetails(m_itemId, this, [this, generation](api::Result<api::Item> result) {
      if (generation != m_generation) return;
      if (!result) {
        setError(result.error().message);
        done();
        return;
      }
      showItem(result.value());
      loadChildren(generation);
      done();
    });
  }

  void ItemDetails::showItem(const api::Item& item) {
    m_item = item;
    const qreal dpr = m_options.devicePixelRatio;
    m_posterUrl = cardImageUrl(m_client, item, CardShape::Poster,
                               cardImageSize(CardShape::Poster, m_options.posterWidth, dpr));
    m_backdropUrl = library::backdropUrl(
        m_client, item, cardImageSize(CardShape::Thumb, m_options.backdropWidth, dpr));
    m_logoUrl = library::logoUrl(m_client, item, static_cast<int>(600 * dpr));
    Q_EMIT itemChanged();
    Q_EMIT userDataChanged();
    if (isPlayable(item)) setTarget(item);
  }

  void ItemDetails::loadChildren(quint64 generation) {
    const api::Item& item = *m_item;
    if (is(item, "Movie") || is(item, "Series") || is(item, "Video")) {
      begin();
      m_client.similarItems(item.id, m_options.similarLimit, this,
                            [this, generation](api::Result<api::ItemPage> result) {
                              if (generation != m_generation) return;
                              if (result) m_similar.setEntries(entries(result->items, false));
                              done();
                            });
    }

    const QString seriesId = is(item, "Series") ? item.id : item.seriesId;
    if (seriesId.isEmpty()) return;

    // Seasons for a series, a season or an episode, so the page can switch.
    begin();
    m_client.seasons(seriesId, this, [this, generation](api::Result<api::ItemPage> result) {
      if (generation != m_generation) return;
      if (!result) {
        setError(result.error().message);
        done();
        return;
      }
      m_seasons.setEntries(entries(result->items, false));
      const api::Item& shown = *m_item;
      QString season;
      if (is(shown, "Season"))
        season = shown.id;
      else if (is(shown, "Episode"))
        season = shown.seasonId;
      if (season.isEmpty() && m_seasons.count() > 0)
        // A series shows the season of its next-up episode once that is
        // known; until then the one shown before a reload, or the first.
        season = m_seasonId.isEmpty() ? m_seasons.entry(0)->item.id : m_seasonId;
      if (!season.isEmpty()) showSeason(season);
      done();
    });

    if (is(item, "Series")) {
      begin();
      m_client.nextUpForSeries(seriesId, this,
                               [this, generation](api::Result<api::ItemPage> result) {
                                 if (generation != m_generation) return;
                                 if (result && !result->items.isEmpty()) {
                                   const api::Item next = result->items.front();
                                   setTarget(next);
                                   if (!next.seasonId.isEmpty()) selectSeason(next.seasonId);
                                 }
                                 done();
                               });
    }
  }

  void ItemDetails::selectSeason(const QString& seasonId) {
    if (m_seasonId == seasonId) return;
    showSeason(seasonId);
  }

  void ItemDetails::showSeason(const QString& seasonId) {
    if (m_seasonId != seasonId) {
      m_seasonId = seasonId;
      Q_EMIT seasonIdChanged();
    }
    if (m_item) loadEpisodes(m_generation, seasonId);
  }

  void ItemDetails::loadEpisodes(quint64 generation, const QString& seasonId) {
    const QString seriesId = is(*m_item, "Series") ? m_item->id : m_item->seriesId;
    if (seriesId.isEmpty() || seasonId.isEmpty()) return;
    begin();
    m_client.episodes(seriesId, seasonId, this,
                      [this, generation, seasonId](api::Result<api::ItemPage> result) {
                        if (generation != m_generation) return;
                        if (seasonId != m_seasonId) {
                          done();
                          return;
                        }
                        if (result) {
                          m_episodes.setEntries(entries(result->items, true));
                          chooseSeasonTarget();
                        } else {
                          setError(result.error().message);
                        }
                        done();
                      });
  }

  void ItemDetails::chooseSeasonTarget() {
    if (!m_item) return;
    // A series without next-up (nothing started) plays its first episode; a
    // season plays its first unwatched one.
    const bool series = is(*m_item, "Series");
    const bool season = is(*m_item, "Season");
    if (!series && !season) return;
    if (series && m_target) return;
    std::optional<api::Item> pick;
    for (int row = 0; row < m_episodes.count(); ++row) {
      const api::Item& episode = m_episodes.entry(row)->item;
      if (!pick) pick = episode;
      if (season && !episode.userData.played) {
        pick = episode;
        break;
      }
    }
    if (pick) setTarget(pick);
  }

  void ItemDetails::setTarget(std::optional<api::Item> target) {
    m_target = std::move(target);
    Q_EMIT playTargetChanged();
  }

  // --- Actions -------------------------------------------------------------

  void ItemDetails::play() {
    if (m_target) Q_EMIT playRequested(m_target->id, 0, false);
  }

  void ItemDetails::resume() {
    if (m_target) Q_EMIT playRequested(m_target->id, resumePositionTicks(), false);
  }

  void ItemDetails::watchTogether() {
    if (m_target) Q_EMIT playRequested(m_target->id, resumePositionTicks(), true);
  }

  void ItemDetails::playEpisode(int row) {
    const auto* entry = m_episodes.entry(row);
    if (!entry) return;
    Q_EMIT playRequested(entry->item.id, entry->item.userData.playbackPositionTicks, false);
  }

  void ItemDetails::toggleFavorite() {
    if (!m_item) return;
    const QString id = m_item->id;
    const bool wanted = !m_item->userData.isFavorite;
    m_client.setFavorite(id, wanted, this, [this](api::Result<api::UserItemData> result) {
      if (!result) {
        setError(result.error().message);
        return;
      }
      if (m_updates)
        m_updates->publish(result.value());
      else
        applyUserData(result.value());
    });
  }

  void ItemDetails::togglePlayed() {
    if (!m_item) return;
    api::UserItemData data = m_item->userData;
    data.itemId = m_item->id;
    data.played = !data.played;
    data.playbackPositionTicks = 0;
    auto finish = [this, data](api::Result<api::Empty> result) {
      if (!result) {
        setError(result.error().message);
        return;
      }
      if (m_updates)
        m_updates->publish(data);
      else
        applyUserData(data);
      // A series' or season's episodes all change; the server sends them,
      // but the page reloads them itself in case the socket is down.
      if (m_item && (is(*m_item, "Series") || is(*m_item, "Season"))) reload();
    };
    if (data.played)
      m_client.markPlayed(data.itemId, this, finish);
    else
      m_client.markUnplayed(data.itemId, this, finish);
  }

  void ItemDetails::applyUserData(const api::UserItemData& data) {
    if (m_item && m_item->id == data.itemId) {
      m_item->userData = mergeUserData(m_item->userData, data);
      Q_EMIT userDataChanged();
    }
    if (m_target && m_target->id == data.itemId) {
      m_target->userData = mergeUserData(m_target->userData, data);
      Q_EMIT playTargetChanged();
    }
    m_seasons.applyUserData(data);
    m_episodes.applyUserData(data);
    m_similar.applyUserData(data);
  }

} // namespace jfd::library
