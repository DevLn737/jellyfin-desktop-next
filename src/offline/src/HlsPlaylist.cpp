#include <jfd/offline/HlsPlaylist.h>

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringList>

namespace jfd::offline {

  namespace {
    QStringList playlistLines(const QByteArray& text) {
      QStringList lines;
      for (const QByteArray& raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty()) lines.append(line);
      }
      return lines;
    }
  } // namespace

  QString hlsFirstVariant(const QByteArray& master) {
    const QStringList lines = playlistLines(master);
    if (lines.isEmpty() || lines.first() != QLatin1String("#EXTM3U")) return {};
    bool streamInfo = false;
    for (const QString& line : lines) {
      if (line.startsWith(QLatin1String("#EXT-X-STREAM-INF"))) {
        streamInfo = true;
        continue;
      }
      if (line.startsWith(QLatin1Char('#'))) continue;
      if (streamInfo) return line;
    }
    return {};
  }

  QString HlsMediaPlaylist::signature() const {
    QByteArray text = mapUri.isEmpty() ? QByteArrayLiteral("nomap") : QByteArrayLiteral("map");
    for (const HlsSegment& segment : segments)
      text += ';' + QByteArray::number(segment.seconds, 'f', 3);
    return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex());
  }

  std::optional<HlsMediaPlaylist> parseHlsMediaPlaylist(const QByteArray& text) {
    const QStringList lines = playlistLines(text);
    if (lines.isEmpty() || lines.first() != QLatin1String("#EXTM3U")) return std::nullopt;
    static const QRegularExpression mapPattern(QStringLiteral("URI=\"([^\"]+)\""));
    HlsMediaPlaylist playlist;
    bool endList = false;
    std::optional<double> pending;
    for (const QString& line : lines) {
      if (line.startsWith(QLatin1String("#EXT-X-PLAYLIST-TYPE:"))) {
        if (line.mid(21).trimmed() != QLatin1String("VOD")) return std::nullopt;
      } else if (line.startsWith(QLatin1String("#EXT-X-MAP:"))) {
        const auto match = mapPattern.match(line);
        if (!match.hasMatch()) return std::nullopt;
        playlist.mapUri = match.captured(1);
      } else if (line.startsWith(QLatin1String("#EXTINF:"))) {
        bool ok = false;
        const double seconds = line.mid(8).section(QLatin1Char(','), 0, 0).trimmed().toDouble(&ok);
        if (!ok || seconds < 0) return std::nullopt;
        pending = seconds;
      } else if (line == QLatin1String("#EXT-X-ENDLIST")) {
        endList = true;
      } else if (!line.startsWith(QLatin1Char('#'))) {
        if (!pending) return std::nullopt;
        playlist.segments.append({line, *pending});
        playlist.totalSeconds += *pending;
        pending.reset();
      }
    }
    if (!endList || playlist.segments.isEmpty()) return std::nullopt;
    return playlist;
  }

} // namespace jfd::offline
