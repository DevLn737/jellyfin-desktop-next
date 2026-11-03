#include <jfd/ui/MusicLyrics.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ErrorText.h>

#include <cmath>

namespace jfd::ui {

  namespace {
    constexpr double TicksPerSecond = 10'000'000.0;
  } // namespace

  MusicLyrics::MusicLyrics(api::JellyfinClient& client, QObject* parent)
      : QAbstractListModel(parent), m_api(client) {}

  int MusicLyrics::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant MusicLyrics::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.parent().isValid() || index.row() >= count()) return {};
    const api::LyricLine& line = m_lyrics.lines[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TextRole:
      return line.text;
    case StartRole:
      return line.startTicks ? static_cast<double>(*line.startTicks) / TicksPerSecond : -1.0;
    case IsCurrentRole:
      return index.row() == m_current;
    default:
      return {};
    }
  }

  QHash<int, QByteArray> MusicLyrics::roleNames() const {
    return {{TextRole, "text"}, {StartRole, "start"}, {IsCurrentRole, "isCurrent"}};
  }

  void MusicLyrics::setTrackId(const QString& id) {
    if (id == m_trackId) return;
    m_trackId = id;
    Q_EMIT trackIdChanged();
    reload();
  }

  void MusicLyrics::setPosition(double seconds) {
    if (seconds == m_position) return;
    m_position = seconds;
    Q_EMIT positionChanged();
    updateCurrent();
  }

  void MusicLyrics::reload() {
    const quint64 serial = ++m_serial;
    m_errorText.clear();
    // The lines of the track before go at once.
    setLyrics({});
    m_loading = !m_trackId.isEmpty();
    Q_EMIT lyricsChanged();
    if (m_trackId.isEmpty()) return;
    m_api.lyrics(m_trackId, this, [this, serial](api::Result<api::Lyrics> result) {
      if (serial != m_serial) return;
      m_loading = false;
      if (!result) {
        m_errorText = library::errorText(result.error());
        Q_EMIT lyricsChanged();
        return;
      }
      setLyrics(std::move(result).value());
      Q_EMIT lyricsChanged();
    });
  }

  void MusicLyrics::setLyrics(api::Lyrics lyrics) {
    const bool hadLine = m_current >= 0;
    beginResetModel();
    m_lyrics = std::move(lyrics);
    m_current = -1;
    endResetModel();
    if (hadLine) Q_EMIT currentLineChanged();
    updateCurrent();
  }

  void MusicLyrics::updateCurrent() {
    const int line = m_lyrics.lineAt(static_cast<qint64>(std::llround(m_position * TicksPerSecond)));
    if (line == m_current) return;
    const int before = m_current;
    m_current = line;
    if (before >= 0 && before < count()) Q_EMIT dataChanged(index(before), index(before), {IsCurrentRole});
    if (line >= 0) Q_EMIT dataChanged(index(line), index(line), {IsCurrentRole});
    Q_EMIT currentLineChanged();
  }

} // namespace jfd::ui
