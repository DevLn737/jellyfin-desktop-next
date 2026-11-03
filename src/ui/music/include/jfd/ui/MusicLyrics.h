#pragma once

#include <jfd/api/MusicApi.h>

#include <QAbstractListModel>
#include <QString>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::ui {

  // The lyrics of the track that plays, for "Now playing": one row per line.
  // The page sets `trackId` and `position` from the player; for synced
  // lyrics `currentLine` follows the position, for plain ones it stays -1.
  // A track without lyrics has none (`empty`), which is not an error.
  class MusicLyrics : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    // Seconds into the track.
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY lyricsChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY lyricsChanged)
    // Asked for and answered: the track has no lyrics.
    Q_PROPERTY(bool empty READ empty NOTIFY lyricsChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY lyricsChanged)
    Q_PROPERTY(int count READ count NOTIFY lyricsChanged)
    Q_PROPERTY(int currentLine READ currentLine NOTIFY currentLineChanged)

  public:
    enum Role { TextRole = Qt::UserRole + 1, StartRole, IsCurrentRole };

    explicit MusicLyrics(api::JellyfinClient& client, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString trackId() const { return m_trackId; }
    void setTrackId(const QString& id);
    double position() const { return m_position; }
    void setPosition(double seconds);
    bool loading() const { return m_loading; }
    bool synced() const { return m_lyrics.synced; }
    bool empty() const { return !m_loading && m_errorText.isEmpty() && !m_trackId.isEmpty() && m_lyrics.isEmpty(); }
    QString errorText() const { return m_errorText; }
    int count() const { return static_cast<int>(m_lyrics.lines.size()); }
    int currentLine() const { return m_current; }

    // Asks for the lyrics again (after an error).
    Q_INVOKABLE void reload();

  Q_SIGNALS:
    void trackIdChanged();
    void positionChanged();
    void lyricsChanged();
    void currentLineChanged();

  private:
    void setLyrics(api::Lyrics lyrics);
    void updateCurrent();

    api::MusicApi m_api;
    QString m_trackId;
    double m_position = 0.0;
    api::Lyrics m_lyrics;
    bool m_loading = false;
    QString m_errorText;
    int m_current = -1;
    quint64 m_serial = 0;
  };

} // namespace jfd::ui
