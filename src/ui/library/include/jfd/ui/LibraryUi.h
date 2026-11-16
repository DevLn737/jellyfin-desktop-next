#pragma once

#include <jfd/library/HomeModel.h>
#include <jfd/library/ItemDetails.h>
#include <jfd/library/LibraryModel.h>
#include <jfd/library/LiveTvChannels.h>
#include <jfd/library/LiveTvGuide.h>
#include <jfd/library/LiveTvRecorder.h>
#include <jfd/library/LiveTvRecordings.h>
#include <jfd/library/LiveTvSchedule.h>
#include <jfd/library/SearchModel.h>
#include <jfd/library/SignIn.h>

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <qqmlintegration.h>

#include <functional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {
  class CollectionIndex;
  class LibraryUpdates;
}

namespace jfd::ui {

  // The library screens' way into the library module (QML: `Library`): the
  // sign-in flow, the home page, search, models for a library grid or an
  // item page, image URLs for the image provider, and Play / Watch together.
  //
  // The engine creates the singleton; the application attaches the
  // services before the window loads. Play and Watch together go to
  // handlers the playback and group modules set; until one is set the
  // button says that it is not available yet.
  class LibraryUi : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Library)
    QML_SINGLETON
    Q_PROPERTY(jfd::library::SignIn* signIn READ signIn NOTIFY attached)
    Q_PROPERTY(jfd::library::HomeModel* home READ home NOTIFY attached)
    Q_PROPERTY(jfd::library::SearchModel* search READ search NOTIFY attached)
    // The Live TV channel list (route "livetv").
    Q_PROPERTY(jfd::library::LiveTvChannels* liveTv READ liveTv NOTIFY attached)
    // The server's DVR: whether this user may record, recording and
    // cancelling, the recordings to come (route "recordings").
    Q_PROPERTY(jfd::library::LiveTvRecorder* recorder READ recorder NOTIFY attached)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY handlersChanged)
    Q_PROPERTY(bool canPlayTracks READ canPlayTracks NOTIFY handlersChanged)
    Q_PROPERTY(bool canPlayChannels READ canPlayChannels NOTIFY handlersChanged)
    // The server's live connection (the session socket) is up. A screen
    // whose request failed while it is down says "No connection", and
    // reloads on reconnected().
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)

  public:
    struct Services {
      api::JellyfinClient* client = nullptr;
      library::SignIn* signIn = nullptr;
      library::HomeModel* home = nullptr;
      library::SearchModel* search = nullptr;
      library::LiveTvChannels* liveTv = nullptr;
      library::LiveTvRecorder* recorder = nullptr;
      library::LibraryUpdates* updates = nullptr;
      library::CollectionIndex* collections = nullptr;
      qreal devicePixelRatio = 1.0;
      // Each library's sort and filters, keyed by library id.
      std::function<QJsonObject()> readFilters;
      std::function<void(const QJsonObject&)> writeFilters;
    };

    using PlayHandler = std::function<void(const QString& itemId, qint64 startPositionTicks)>;
    // A track found by search plays in the music player, its album as the
    // queue. `play` answers false when music cannot play now (in a group);
    // `stopsVideo` says that starting it would stop the video playing.
    struct TrackHandler {
      std::function<bool(const QString& trackId, const QString& albumId)> play;
      std::function<bool()> stopsVideo;
    };

    explicit LibraryUi(QObject* parent = nullptr);

    void attach(const Services& services);
    void setPlayHandler(PlayHandler handler);
    void setTrackHandler(TrackHandler handler);
    // A Live TV channel plays on this device only, never for a group (a
    // live stream has no shared position).
    void setChannelHandler(std::function<void(const QString& channelId)> handler);

    library::SignIn* signIn() const { return m_services.signIn; }
    library::HomeModel* home() const { return m_services.home; }
    library::SearchModel* search() const { return m_services.search; }
    library::LiveTvChannels* liveTv() const { return m_services.liveTv; }
    library::LiveTvRecorder* recorder() const { return m_services.recorder; }
    bool canPlay() const { return bool(m_play); }
    bool canPlayTracks() const { return bool(m_track.play); }
    bool canPlayChannels() const { return bool(m_channel); }
    bool online() const { return m_online; }
    // Called by LibraryScreens as the session socket comes and goes; a
    // change from offline to online emits reconnected().
    void setOnline(bool online);

    // A grid for one library, owned by `owner` (the page), with the sort
    // and filters it had last time; changes to them are kept. Null before
    // attach.
    Q_INVOKABLE jfd::library::LibraryModel* createLibrary(const QString& libraryId, QObject* owner);
    // A grid of one collection's items, owned by `owner`. Null before attach.
    Q_INVOKABLE jfd::library::LibraryModel* createCollection(const QString& collectionId,
                                                             QObject* owner);
    // The films and series a person appears in, owned by `owner`. Null
    // before attach.
    Q_INVOKABLE jfd::library::LibraryModel* createPerson(const QString& personId, QObject* owner);
    // An item page's model, owned by `owner`. Null before attach.
    Q_INVOKABLE jfd::library::ItemDetails* createDetails(const QString& itemId, QObject* owner);
    // A search of its own (the command palette's), owned by `owner`; the
    // search page keeps using `search`. Null before attach.
    Q_INVOKABLE jfd::library::SearchModel* createSearch(QObject* owner);
    // The programme guide (route "guide") and one channel's programmes
    // (route "channel/<id>"), owned by `owner`. Both mark what the
    // recorder records, and follow it as that changes.
    Q_INVOKABLE jfd::library::LiveTvGuide* createGuide(QObject* owner);
    Q_INVOKABLE jfd::library::LiveTvSchedule* createSchedule(const QString& channelId, QObject* owner);
    // The recordings (route "recordings"), owned by `owner`; read again
    // when the recorder's timers change (a recording starts or stops).
    Q_INVOKABLE jfd::library::LiveTvRecordings* createRecordings(QObject* owner);
    // The image provider's source for a server image URL.
    Q_INVOKABLE QString image(const QUrl& url) const;
    // Where a card leads: "library/<id>" for a library, "collection/<id>"
    // for a collection, "person/<id>" for a person, the music pages for
    // albums, artists and playlists, "livetv" for the Live TV library,
    // "channel/<id>" for a channel, "item/<id>" for anything else. A track has no page: it
    // plays (playTrack).
    Q_INVOKABLE QString routeFor(const QString& itemId, const QString& type) const;
    // A library's title from the home page's list, for the grid's heading.
    Q_INVOKABLE QString libraryName(const QString& libraryId) const;
    // "1 h 52 min", "45 min".
    Q_INVOKABLE QString formatRunTime(qint64 ticks) const;

    Q_INVOKABLE void play(const QString& itemId, qint64 startPositionTicks);
    // A card's or the banner's quick actions. The answer reaches every
    // screen through LibraryUpdates; a failure is a message.
    Q_INVOKABLE void setFavorite(const QString& itemId, bool favorite);
    // `favorite` is the item's current favorite state: the update carries
    // the whole user data, as the server's own messages do.
    Q_INVOKABLE void setPlayed(const QString& itemId, bool played, bool favorite);
    // Tunes to a Live TV channel (setChannelHandler).
    Q_INVOKABLE void playChannel(const QString& channelId);
    // Plays a track in its album. With a video playing it first asks
    // (trackConfirmationNeeded); `confirmed` is the answer.
    Q_INVOKABLE void playTrack(const QString& trackId, const QString& albumId,
                               bool confirmed = false);

  Q_SIGNALS:
    void attached();
    void handlersChanged();
    // For the shell's toast.
    void message(const QString& text, bool error);
    void onlineChanged();
    // The connection came back after it was lost: screens showing an
    // error load again.
    void reconnected();
    // Playing this track would stop the video; the window asks, and calls
    // playTrack again with `confirmed` on yes.
    void trackConfirmationNeeded(const QString& trackId, const QString& albumId);

  private:
    QString collectionTypeOf(const QString& libraryId) const;
    library::LibraryModel* newModel(QObject* owner) const;

    // Filters of the libraries opened this session, when there is no store.
    QJsonObject m_filters;

    Services m_services;
    PlayHandler m_play;
    TrackHandler m_track;
    std::function<void(const QString&)> m_channel;
    bool m_online = true;
  };

} // namespace jfd::ui
