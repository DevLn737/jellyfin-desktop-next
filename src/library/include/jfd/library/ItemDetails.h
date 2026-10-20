#pragma once

#include <jfd/api/Models.h>
#include <jfd/library/ItemListModel.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  class LibraryUpdates;

  // The page of one item: a movie, a series with its seasons and episodes, a
  // season, or an episode with the rest of its season.
  //
  // "Play" means what Jellyfin Web means: a movie or episode plays itself
  // (from the resume point with resume()), a series plays its next-up
  // episode, a season its first unwatched one. The page does not start
  // playback: it emits playRequested, which the app hands to the session's
  // PlaybackLauncher.
  class ItemDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString itemId READ itemId WRITE setItemId NOTIFY itemIdChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY itemChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

    Q_PROPERTY(QString name READ name NOTIFY itemChanged)
    Q_PROPERTY(QString type READ type NOTIFY itemChanged)
    Q_PROPERTY(QString overview READ overview NOTIFY itemChanged)
    Q_PROPERTY(QVariant year READ year NOTIFY itemChanged)
    Q_PROPERTY(QStringList genres READ genres NOTIFY itemChanged)
    Q_PROPERTY(QVariant communityRating READ communityRating NOTIFY itemChanged)
    Q_PROPERTY(QString officialRating READ officialRating NOTIFY itemChanged)
    Q_PROPERTY(qint64 runTimeTicks READ runTimeTicks NOTIFY itemChanged)
    Q_PROPERTY(QString seriesId READ seriesId NOTIFY itemChanged)
    Q_PROPERTY(QString seriesName READ seriesName NOTIFY itemChanged)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY itemChanged)
    Q_PROPERTY(QUrl posterUrl READ posterUrl NOTIFY itemChanged)
    Q_PROPERTY(QUrl backdropUrl READ backdropUrl NOTIFY itemChanged)
    Q_PROPERTY(QUrl logoUrl READ logoUrl NOTIFY itemChanged)

    Q_PROPERTY(bool played READ played NOTIFY userDataChanged)
    Q_PROPERTY(bool favorite READ favorite NOTIFY userDataChanged)
    Q_PROPERTY(qint64 playbackPositionTicks READ playbackPositionTicks NOTIFY userDataChanged)
    Q_PROPERTY(double progress READ progress NOTIFY userDataChanged)

    // What "Play" starts, and from where "Resume" would.
    Q_PROPERTY(QString playTargetId READ playTargetId NOTIFY playTargetChanged)
    Q_PROPERTY(QString playTargetName READ playTargetName NOTIFY playTargetChanged)
    Q_PROPERTY(qint64 resumePositionTicks READ resumePositionTicks NOTIFY playTargetChanged)

    Q_PROPERTY(jfd::library::ItemListModel* seasons READ seasons CONSTANT)
    Q_PROPERTY(jfd::library::ItemListModel* episodes READ episodes CONSTANT)
    Q_PROPERTY(jfd::library::ItemListModel* similar READ similar CONSTANT)
    Q_PROPERTY(QString seasonId READ seasonId WRITE selectSeason NOTIFY seasonIdChanged)

  public:
    struct Options {
      int posterWidth = 300;
      int episodeWidth = 320;
      int backdropWidth = 1920;
      int similarLimit = 12;
      qreal devicePixelRatio = 1.0;
    };

    ItemDetails(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit ItemDetails(api::JellyfinClient& client, QObject* parent = nullptr)
        : ItemDetails(client, Options{}, parent) {}

    QString itemId() const { return m_itemId; }
    void setItemId(const QString& id);
    bool loading() const { return m_pending > 0; }
    bool loaded() const { return m_item.has_value(); }
    QString errorText() const { return m_errorText; }
    const std::optional<api::Item>& item() const { return m_item; }

    QString name() const;
    QString type() const;
    QString overview() const;
    QVariant year() const;
    QStringList genres() const;
    QVariant communityRating() const;
    QString officialRating() const;
    qint64 runTimeTicks() const;
    QString seriesId() const;
    QString seriesName() const;
    // "S1:E3" for an episode, "Season 2" style names come from the server.
    QString subtitle() const;
    QUrl posterUrl() const { return m_posterUrl; }
    QUrl backdropUrl() const { return m_backdropUrl; }
    QUrl logoUrl() const { return m_logoUrl; }

    bool played() const;
    bool favorite() const;
    qint64 playbackPositionTicks() const;
    double progress() const;

    QString playTargetId() const { return m_target ? m_target->id : QString(); }
    QString playTargetName() const { return m_target ? m_target->name : QString(); }
    qint64 resumePositionTicks() const;

    ItemListModel* seasons() { return &m_seasons; }
    ItemListModel* episodes() { return &m_episodes; }
    ItemListModel* similar() { return &m_similar; }
    QString seasonId() const { return m_seasonId; }
    void selectSeason(const QString& seasonId);

    void setUpdates(LibraryUpdates* updates);

    // Starts the play target from the beginning.
    Q_INVOKABLE void play();
    // Starts the play target from its resume point.
    Q_INVOKABLE void resume();
    // Creates a SyncPlay group with the play target (the session decides).
    Q_INVOKABLE void watchTogether();
    Q_INVOKABLE void playEpisode(int row);
    Q_INVOKABLE void toggleFavorite();
    Q_INVOKABLE void togglePlayed();
    Q_INVOKABLE void reload();

  Q_SIGNALS:
    void itemIdChanged();
    void loadingChanged();
    void errorTextChanged();
    void itemChanged();
    void userDataChanged();
    void playTargetChanged();
    void seasonIdChanged();
    // `startPositionTicks` 0 starts from the beginning.
    void playRequested(const QString& itemId, qint64 startPositionTicks, bool together);

  private:
    void begin();
    void done();
    void setError(const QString& text);
    void showItem(const api::Item& item);
    void loadChildren(quint64 generation);
    void showSeason(const QString& seasonId);
    void loadEpisodes(quint64 generation, const QString& seasonId);
    void setTarget(std::optional<api::Item> target);
    void chooseSeasonTarget();
    void applyUserData(const api::UserItemData& data);
    QList<ItemListModel::Entry> entries(const QList<api::Item>& items, bool episodes) const;

    api::JellyfinClient& m_client;
    Options m_options;
    LibraryUpdates* m_updates = nullptr;
    QString m_itemId;
    quint64 m_generation = 0;
    int m_pending = 0;
    QString m_errorText;
    std::optional<api::Item> m_item;
    std::optional<api::Item> m_target;
    QUrl m_posterUrl;
    QUrl m_backdropUrl;
    QUrl m_logoUrl;
    ItemListModel m_seasons;
    ItemListModel m_episodes;
    ItemListModel m_similar;
    QString m_seasonId;
  };

} // namespace jfd::library
