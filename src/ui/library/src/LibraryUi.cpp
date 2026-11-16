#include "jfd/ui/LibraryUi.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/CollectionIndex.h>
#include <jfd/library/ErrorText.h>
#include <jfd/library/ItemListModel.h>
#include <jfd/library/LibraryUpdates.h>
#include <jfd/ui/LibraryImageProvider.h>

#include <QLoggingCategory>
#include <QQmlEngine>

namespace jfd::ui {

  namespace {
    Q_LOGGING_CATEGORY(lcLibraryUi, "jfd.ui.library")

    constexpr qint64 TicksPerMinute = 600'000'000;
  } // namespace

  LibraryUi::LibraryUi(QObject* parent) : QObject(parent) {}

  void LibraryUi::attach(const Services& services) {
    if (m_services.recorder != nullptr) disconnect(m_services.recorder, nullptr, this, nullptr);
    m_services = services;
    if (m_services.recorder != nullptr)
      connect(m_services.recorder, &library::LiveTvRecorder::message, this, &LibraryUi::message);
    Q_EMIT attached();
  }

  void LibraryUi::setOnline(bool online) {
    if (online == m_online) return;
    m_online = online;
    Q_EMIT onlineChanged();
    if (online) Q_EMIT reconnected();
  }

  void LibraryUi::setPlayHandler(PlayHandler handler) {
    m_play = std::move(handler);
    Q_EMIT handlersChanged();
  }

  void LibraryUi::setWatchTogetherHandler(WatchTogetherHandler handler) {
    m_watchTogether = std::move(handler);
    Q_EMIT handlersChanged();
  }

  void LibraryUi::setTrackHandler(TrackHandler handler) {
    m_track = std::move(handler);
    Q_EMIT handlersChanged();
  }

  void LibraryUi::setChannelHandler(std::function<void(const QString&)> handler) {
    m_channel = std::move(handler);
    Q_EMIT handlersChanged();
  }

  library::LibraryModel* LibraryUi::newModel(QObject* owner) const {
    library::LibraryModel::Options options;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* model = new library::LibraryModel(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    if (m_services.updates != nullptr) model->follow(*m_services.updates);
    return model;
  }

  library::SearchModel* LibraryUi::createSearch(QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::SearchModel::Options options;
    // The palette shows a few results and answers quickly.
    options.delayMs = 200;
    options.limit = 12;
    options.peopleLimit = 4;
    options.cardWidth = 80;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* model = new library::SearchModel(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    if (m_services.updates != nullptr) model->follow(*m_services.updates);
    return model;
  }

  library::LibraryModel* LibraryUi::createLibrary(const QString& libraryId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LibraryModel* model = newModel(owner);
    // The collection type decides the item types, so it goes first.
    model->setCollectionType(collectionTypeOf(libraryId));
    model->setLibraryId(libraryId);
    const QJsonObject saved = m_services.readFilters ? m_services.readFilters() : m_filters;
    model->applyFilters(saved.value(libraryId).toObject());
    connect(model, &library::LibraryModel::queryChanged, this, [this, model, libraryId] {
      QJsonObject all = m_services.readFilters ? m_services.readFilters() : m_filters;
      const QJsonObject filters = model->filters();
      if (all.value(libraryId).toObject() == filters) return;
      all.insert(libraryId, filters);
      if (m_services.writeFilters)
        m_services.writeFilters(all);
      else
        m_filters = all;
    });
    return model;
  }

  library::LibraryModel* LibraryUi::createCollection(const QString& collectionId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LibraryModel* model = newModel(owner);
    model->setCollectionType(library::LibraryModel::CollectionItems);
    // By release date, oldest first: a film series in its order.
    model->setSortBy(QStringLiteral("PremiereDate"));
    model->setLibraryId(collectionId);
    return model;
  }

  library::LibraryModel* LibraryUi::createPerson(const QString& personId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LibraryModel* model = newModel(owner);
    model->setCollectionType(library::LibraryModel::PersonItems);
    // Newest first, as on a filmography.
    model->setSortBy(QStringLiteral("PremiereDate"));
    model->setDescending(true);
    model->setLibraryId(personId);
    return model;
  }

  library::LiveTvGuide* LibraryUi::createGuide(QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LiveTvGuide::Options options;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* guide = new library::LiveTvGuide(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(guide, QQmlEngine::CppOwnership);
    if (library::LiveTvRecorder* recorder = m_services.recorder) {
      connect(recorder, &library::LiveTvRecorder::timersChanged, guide,
              [guide, recorder] { guide->applyTimers(recorder->timers()); });
    }
    return guide;
  }

  library::LiveTvSchedule* LibraryUi::createSchedule(const QString& channelId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LiveTvSchedule::Options options;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* schedule = new library::LiveTvSchedule(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(schedule, QQmlEngine::CppOwnership);
    if (library::LiveTvRecorder* recorder = m_services.recorder) {
      connect(recorder, &library::LiveTvRecorder::timersChanged, schedule,
              [schedule, recorder] { schedule->applyTimers(recorder->timers()); });
    }
    schedule->setChannelId(channelId);
    return schedule;
  }

  library::LiveTvRecordings* LibraryUi::createRecordings(QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LiveTvRecordings::Options options;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* recordings = new library::LiveTvRecordings(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(recordings, QQmlEngine::CppOwnership);
    connect(recordings, &library::LiveTvRecordings::failed, this,
            [this](const QString& text) { Q_EMIT message(text, true); });
    connect(recordings, &library::LiveTvRecordings::removed, this,
            [this] { Q_EMIT message(tr("Recording deleted."), false); });
    if (library::LiveTvRecorder* recorder = m_services.recorder)
      connect(recorder, &library::LiveTvRecorder::timersChanged, recordings,
              &library::LiveTvRecordings::reload);
    return recordings;
  }

  library::ItemDetails* LibraryUi::createDetails(const QString& itemId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::ItemDetails::Options options;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* details = new library::ItemDetails(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(details, QQmlEngine::CppOwnership);
    details->setUpdates(m_services.updates);
    details->setCollectionIndex(m_services.collections);
    connect(details, &library::ItemDetails::playRequested, this,
            [this, details](const QString& id, qint64 startTicks, bool together) {
              if (together) {
                watchTogether(id, details->name(), startTicks);
              } else {
                play(id, startTicks);
              }
            });
    details->setItemId(itemId);
    return details;
  }

  QString LibraryUi::image(const QUrl& url) const {
    return LibraryImageProvider::sourceFor(url);
  }

  QString LibraryUi::routeFor(const QString& itemId, const QString& type) const {
    if (type == QLatin1String("BoxSet")) return QStringLiteral("collection/") + itemId;
    // Music results of a search; their screens are in src/ui/music.
    if (type == QLatin1String("MusicAlbum")) return QStringLiteral("album/") + itemId;
    if (type == QLatin1String("MusicArtist")) return QStringLiteral("artist/") + itemId;
    if (type == QLatin1String("Playlist")) return QStringLiteral("playlist/") + itemId;
    if (type == QLatin1String("Person")) return QStringLiteral("person/") + itemId;
    if (type == QLatin1String("TvChannel") || type == QLatin1String("LiveTvChannel"))
      return QStringLiteral("channel/") + itemId;
    if (type == QLatin1String("CollectionFolder") || type == QLatin1String("UserView") ||
        type == QLatin1String("Folder")) {
      // Music libraries have screens of their own (src/ui/music).
      const QString collectionType = collectionTypeOf(itemId);
      if (collectionType == QLatin1String("music")) return QStringLiteral("music");
      // The Live TV library is the channel list.
      if (collectionType == QLatin1String("livetv")) return QStringLiteral("livetv");
      return QStringLiteral("library/") + itemId;
    }
    // A Seerr title (id like "movie:603"), added to the home screen by the
    // Seerr module.
    if (type == QLatin1String("SeerrTitle")) return QStringLiteral("seerr-title/") + itemId;
    return QStringLiteral("item/") + itemId;
  }

  QString LibraryUi::libraryName(const QString& libraryId) const {
    if (m_services.home == nullptr) return {};
    const library::ItemListModel* libraries = m_services.home->libraries();
    const int row = libraries->rowOf(libraryId);
    return row < 0 ? QString() : libraries->entry(row)->item.name;
  }

  QString LibraryUi::collectionTypeOf(const QString& libraryId) const {
    if (m_services.home == nullptr) return {};
    const library::ItemListModel* libraries = m_services.home->libraries();
    const int row = libraries->rowOf(libraryId);
    return row < 0 ? QString() : libraries->entry(row)->item.collectionType;
  }

  QString LibraryUi::formatRunTime(qint64 ticks) const {
    const qint64 minutes = (ticks + TicksPerMinute / 2) / TicksPerMinute;
    if (minutes <= 0) return {};
    if (minutes < 60) return tr("%n min", nullptr, int(minutes));
    const qint64 hours = minutes / 60;
    const qint64 rest = minutes % 60;
    return rest == 0 ? tr("%n h", nullptr, int(hours)) : tr("%1 h %2 min").arg(hours).arg(rest);
  }

  void LibraryUi::play(const QString& itemId, qint64 startPositionTicks) {
    if (itemId.isEmpty()) return;
    if (!m_play) {
      qCInfo(lcLibraryUi) << "Play" << itemId << "at" << startPositionTicks
                          << "with no player installed";
      Q_EMIT message(tr("Playback is not available in this build yet."), false);
      return;
    }
    m_play(itemId, startPositionTicks);
  }

  void LibraryUi::setFavorite(const QString& itemId, bool favorite) {
    if (itemId.isEmpty() || m_services.client == nullptr) return;
    m_services.client->setFavorite(itemId, favorite, this, [this](api::Result<api::UserItemData> result) {
      if (!result) {
        Q_EMIT message(library::errorText(result.error()), true);
        return;
      }
      if (m_services.updates != nullptr) m_services.updates->publish(result.value());
    });
  }

  void LibraryUi::setPlayed(const QString& itemId, bool played, bool favorite) {
    if (itemId.isEmpty() || m_services.client == nullptr) return;
    api::UserItemData data;
    data.itemId = itemId;
    data.played = played;
    data.isFavorite = favorite;
    auto finish = [this, data](api::Result<api::Empty> result) {
      if (!result) {
        Q_EMIT message(library::errorText(result.error()), true);
        return;
      }
      if (m_services.updates != nullptr) m_services.updates->publish(data);
    };
    if (played)
      m_services.client->markPlayed(itemId, this, finish);
    else
      m_services.client->markUnplayed(itemId, this, finish);
  }

  void LibraryUi::playChannel(const QString& channelId) {
    if (channelId.isEmpty()) return;
    if (!m_channel) {
      qCInfo(lcLibraryUi) << "Channel" << channelId << "with no player installed";
      Q_EMIT message(tr("Playback is not available in this build yet."), false);
      return;
    }
    m_channel(channelId);
  }

  void LibraryUi::watchTogether(const QString& itemId, const QString& name,
                                qint64 startPositionTicks) {
    if (itemId.isEmpty()) return;
    if (!m_watchTogether) {
      qCInfo(lcLibraryUi) << "Watch together" << itemId << "with no group module installed";
      Q_EMIT message(tr("Watching together is not available in this build yet."), false);
      return;
    }
    m_watchTogether(itemId, name, startPositionTicks);
  }

  void LibraryUi::playTrack(const QString& trackId, const QString& albumId, bool confirmed) {
    if (trackId.isEmpty()) return;
    if (!m_track.play) {
      qCInfo(lcLibraryUi) << "Play track" << trackId << "with no music player installed";
      Q_EMIT message(tr("Music is not available in this build yet."), false);
      return;
    }
    if (!confirmed && m_track.stopsVideo && m_track.stopsVideo()) {
      Q_EMIT trackConfirmationNeeded(trackId, albumId);
      return;
    }
    if (!m_track.play(trackId, albumId))
      Q_EMIT message(tr("Leave the group to play music."), false);
  }

} // namespace jfd::ui
