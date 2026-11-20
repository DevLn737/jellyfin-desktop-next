#include <jfd/offline/LibraryNaming.h>

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>

namespace jfd::offline {

  namespace {
    QString text(const QJsonObject& o, const char* key) {
      return o.value(QLatin1String(key)).toString().trimmed();
    }

    std::optional<int> number(const QJsonObject& o, const char* key) {
      const QJsonValue v = o.value(QLatin1String(key));
      if (!v.isDouble()) return std::nullopt;
      return v.toInt();
    }

    QString two(int n) { return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')); }

    // The last part of a server path, whatever its separators.
    QString fileNameOf(const QString& path) {
      const qsizetype cut = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
      return path.mid(cut + 1);
    }

    QString stemOf(const QString& name) {
      const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
      return dot > 0 ? name.left(dot) : name;
    }

    // One dot-separated token of a sidecar name: no dots, safe characters.
    QString token(const QString& value) {
      QString t = value;
      t.replace(QLatin1Char('.'), QLatin1Char(' '));
      return safeFileName(t.simplified(), QString(), 40);
    }

    QString episodeBase(const QJsonObject& item) {
      const QString series = safeFileName(text(item, "SeriesName"), QStringLiteral("Unknown Series"));
      const auto season = number(item, "ParentIndexNumber");
      const auto episode = number(item, "IndexNumber");
      const auto last = number(item, "IndexNumberEnd");
      QString folder = series;
      QString code;
      if (season) {
        folder += QStringLiteral("/Season ") + two(*season);
        code = QStringLiteral("S") + two(*season);
      }
      if (episode) {
        if (code.isEmpty()) code = QStringLiteral("S00");
        code += QStringLiteral("E") + two(*episode);
        if (last && *last > *episode) code += QStringLiteral("-E") + two(*last);
      }
      // The episode's own title, unless it only repeats the file name or
      // the number ("Series S01E02", "Episode 2").
      QString title = text(item, "Name");
      static const QRegularExpression numbered(
          QStringLiteral("(^|\\W)(s\\d+\\s*e\\d+|episode\\s*\\d+|серия\\s*\\d+)(\\W|$)"),
          QRegularExpression::CaseInsensitiveOption);
      if (numbered.match(title).hasMatch() ||
          title.compare(text(item, "SeriesName"), Qt::CaseInsensitive) == 0)
        title.clear();
      QStringList parts{series};
      if (!code.isEmpty()) parts.append(code);
      if (!title.isEmpty()) parts.append(safeFileName(title, QString(), 80));
      if (code.isEmpty() && title.isEmpty()) parts.append(safeFileName(text(item, "Name")));
      return QStringLiteral("Shows/") + folder + QLatin1Char('/') + parts.join(QStringLiteral(" - "));
    }

    QString movieName(const QJsonObject& item) {
      QString name = safeFileName(text(item, "Name"));
      if (const auto year = number(item, "ProductionYear"); year && *year > 0)
        name += QStringLiteral(" (%1)").arg(*year);
      return name;
    }

    QString trackBase(const QJsonObject& item) {
      QString artist = text(item, "AlbumArtist");
      if (artist.isEmpty())
        for (const QJsonValue& a : item.value(QLatin1String("Artists")).toArray())
          if (!a.toString().trimmed().isEmpty()) {
            artist = a.toString().trimmed();
            break;
          }
      const QString album = text(item, "Album");
      QString name = safeFileName(text(item, "Name"));
      if (const auto track = number(item, "IndexNumber"); track && *track > 0) {
        QString prefix = two(*track);
        if (const auto disc = number(item, "ParentIndexNumber"); disc && *disc > 1)
          prefix = QString::number(*disc) + QLatin1Char('-') + prefix;
        name = prefix + QStringLiteral(" - ") + name;
      }
      return QStringLiteral("Music/") + safeFileName(artist, QStringLiteral("Unknown Artist")) +
             QLatin1Char('/') + safeFileName(album, QStringLiteral("Unknown Album")) + QLatin1Char('/') +
             name;
    }
  } // namespace

  QString safeFileName(const QString& name, const QString& fallback, int maxLength) {
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
      if (c.unicode() < 0x20 || c == QChar(0x7f)) continue;
      switch (c.unicode()) {
      case '<': case '>': case '"': case '|': case '?': case '*': out.append(QLatin1Char('_')); break;
      case ':': out.append(QStringLiteral(" -")); break;
      case '/': case '\\': out.append(QLatin1Char('-')); break;
      default: out.append(c);
      }
    }
    out = out.simplified();
    if (out.size() > maxLength) out = out.left(maxLength);
    // Windows drops trailing dots and spaces; a leading dot hides the file.
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' '))))
      out.chop(1);
    while (out.startsWith(QLatin1Char('.'))) out.remove(0, 1);
    if (out.isEmpty()) return fallback;
    static const QRegularExpression reserved(
        QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(out).hasMatch()) out.prepend(QLatin1Char('_'));
    return out;
  }

  QString libraryBase(const QJsonObject& item) {
    const QString type = text(item, "Type");
    if (type == QLatin1String("Episode")) return episodeBase(item);
    if (type == QLatin1String("Movie")) {
      const QString name = movieName(item);
      return QStringLiteral("Movies/") + name + QLatin1Char('/') + name;
    }
    if (type == QLatin1String("Audio")) return trackBase(item);
    if (type == QLatin1String("AudioBook")) {
      QString book = text(item, "Album");
      if (book.isEmpty()) book = text(item, "Name");
      return QStringLiteral("Audiobooks/") + safeFileName(book) + QLatin1Char('/') +
             safeFileName(text(item, "Name"));
    }
    const QString name = movieName(item);
    return QStringLiteral("Videos/") + name + QLatin1Char('/') + name;
  }

  QString sidecarSuffix(const QJsonObject& stream, const QString& mediaPath) {
    const QString streamPath = text(stream, "Path");
    if (!streamPath.isEmpty() && !mediaPath.isEmpty()) {
      const QString stem = stemOf(fileNameOf(mediaPath));
      const QString name = fileNameOf(streamPath);
      if (!stem.isEmpty() && name.size() > stem.size() + 1 &&
          name.startsWith(stem + QLatin1Char('.'), Qt::CaseInsensitive)) {
        // "Video.ru.forced.srt" -> "ru.forced"; "Video.srt" -> "".
        const QStringList parts = stemOf(name.mid(stem.size() + 1)).split(QLatin1Char('.'));
        if (name.mid(stem.size() + 1).contains(QLatin1Char('.'))) {
          QStringList kept;
          for (const QString& part : parts)
            if (const QString t = token(part); !t.isEmpty()) kept.append(t);
          return kept.join(QLatin1Char('.'));
        }
        return {};
      }
    }
    QStringList parts;
    if (const QString title = token(text(stream, "Title")); !title.isEmpty()) parts.append(title);
    const QString language = token(text(stream, "Language"));
    if (!language.isEmpty() && language.compare(QLatin1String("und"), Qt::CaseInsensitive) != 0)
      parts.append(language);
    if (stream.value(QLatin1String("IsDefault")).toBool()) parts.append(QStringLiteral("default"));
    if (stream.value(QLatin1String("IsForced")).toBool()) parts.append(QStringLiteral("forced"));
    if (stream.value(QLatin1String("IsHearingImpaired")).toBool()) parts.append(QStringLiteral("sdh"));
    return parts.join(QLatin1Char('.'));
  }

  QString sidecarPath(const QString& base, const QString& suffix, const QString& extension,
                      const QStringList& taken) {
    const auto make = [&](const QString& tag) {
      QStringList parts{base};
      if (!tag.isEmpty()) parts.append(tag);
      if (!suffix.isEmpty()) parts.append(suffix);
      parts.append(extension);
      return parts.join(QLatin1Char('.'));
    };
    QSet<QString> lower;
    for (const QString& t : taken) lower.insert(t.toLower());
    QString path = make(QString());
    for (int n = 2; lower.contains(path.toLower()) && n < 1000; ++n) path = make(QString::number(n));
    return path;
  }

} // namespace jfd::offline
