#include <jfd/integrations/DiscordActivity.h>

#include <QCoreApplication>
#include <QHostAddress>
#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>

namespace jfd::integrations {

  namespace {
    const QString Logo =
        QStringLiteral("https://raw.githubusercontent.com/jellyfin/jellyfin-media-player/v1.12.0/resources/images/icon.png");

    QString clipped(QString text) {
      text = text.simplified();
      // Discord limits these fields to 128 UTF-8 bytes. Do not split a code point.
      while (text.toUtf8().size() > 128) text.chop(text.back().isLowSurrogate() ? 2 : 1);
      return text;
    }

    QString clockText(qint64 ms) {
      const qint64 seconds = std::max<qint64>(0, ms) / 1000;
      const QLatin1Char zero('0');
      return seconds >= 3600 ? QStringLiteral("%1:%2:%3")
                                   .arg(seconds / 3600)
                                   .arg(seconds / 60 % 60, 2, 10, zero)
                                   .arg(seconds % 60, 2, 10, zero)
                             : QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, zero);
    }

    bool publicHost(const QString& host) {
      QHostAddress address;
      return !host.isEmpty() && !address.setAddress(host) && host.contains(QLatin1Char('.')) &&
             !host.endsWith(QStringLiteral(".localhost")) && !host.endsWith(QStringLiteral(".local")) &&
             !host.endsWith(QStringLiteral(".lan")) && !host.endsWith(QStringLiteral(".internal")) &&
             !host.endsWith(QStringLiteral(".home.arpa"));
    }
  } // namespace

  QUrl publicPosterUrl(const QUrl& serverUrl, const QString& itemId, const QString& imageTag) {
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9-]+$"));
    if (serverUrl.scheme() != QStringLiteral("https") || !publicHost(serverUrl.host().toLower())) return {};
    if (!identifier.match(itemId).hasMatch() || !identifier.match(imageTag).hasMatch()) return {};
    // Keeps reverse-proxy subpaths such as /jellyfin.
    QUrl url = serverUrl.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment |
                                  QUrl::StripTrailingSlash);
    url.setPath(url.path() + QStringLiteral("/Items/%1/Images/Primary").arg(itemId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tag"), imageTag);
    query.addQueryItem(QStringLiteral("maxHeight"), QStringLiteral("512"));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded).size() <= 256 ? url : QUrl();
  }

  qint64 presenceStart(qint64 previousStart, qint64 positionMs, bool playing, qint64 nowSeconds) {
    return !playing && previousStart > 0 ? previousStart
                                         : std::max<qint64>(1, nowSeconds - std::max<qint64>(0, positionMs) / 1000);
  }

  QJsonObject buildActivity(const PresenceInfo& info, qint64 start) {
    const bool episode = info.kind == PresenceInfo::Kind::Episode;
    const bool music = info.kind == PresenceInfo::Kind::Music;
    QString title = episode && !info.seriesName.trimmed().isEmpty() ? info.seriesName : info.title;
    if (title.trimmed().isEmpty()) return {};

    QStringList state;
    if (episode) {
      QStringList number;
      if (info.season) number << QStringLiteral("S%1").arg(*info.season, 2, 10, QLatin1Char('0'));
      if (info.episode) {
        QString e = QStringLiteral("E%1").arg(*info.episode, 2, 10, QLatin1Char('0'));
        if (info.episodeEnd && *info.episodeEnd > *info.episode)
          e += QStringLiteral("–%1").arg(*info.episodeEnd, 2, 10, QLatin1Char('0'));
        number << e;
      }
      state << (number.isEmpty() ? QCoreApplication::translate("DiscordActivity", "Series") : QCoreApplication::translate("DiscordActivity", "Series") + QStringLiteral(" · ") + number.join(QLatin1Char(' ')));
    } else if (music) {
      QStringList line;
      if (!info.artist.trimmed().isEmpty()) line << info.artist;
      if (!info.album.trimmed().isEmpty()) line << info.album;
      state << (line.isEmpty() ? QCoreApplication::translate("DiscordActivity", "Music") : line.join(QStringLiteral(" · ")));
    } else {
      state << (info.year > 0 ? QCoreApplication::translate("DiscordActivity", "Movie") + QStringLiteral(" · ") + QString::number(info.year) : QCoreApplication::translate("DiscordActivity", "Movie"));
    }
    const QString status = info.buffering ? QCoreApplication::translate("DiscordActivity", "Loading") : info.playing ? (music ? QCoreApplication::translate("DiscordActivity", "Listening") : QCoreApplication::translate("DiscordActivity", "Watching")) : QCoreApplication::translate("DiscordActivity", "Paused");
    if (info.groupSize > 1 && !music) state << QCoreApplication::translate("DiscordActivity", "Watching together (%1)").arg(info.groupSize);
    // Discord's timer cannot stop: when paused, the position is in the text.
    if (!info.playing) state << status + QStringLiteral(" · ") + clockText(info.positionMs);

    const QJsonObject assets{{QStringLiteral("large_image"), info.poster.isValid() ? info.poster.toString(QUrl::FullyEncoded) : Logo},
                             {QStringLiteral("large_text"), clipped(title)},
                             {QStringLiteral("small_image"), Logo},
                             {QStringLiteral("small_text"), QStringLiteral("Jellyfin Desktop Next · ") + status}};
    QJsonObject timestamps{{QStringLiteral("start"), static_cast<double>(start)}};
    if (info.durationMs >= 1000)
      timestamps.insert(QStringLiteral("end"), static_cast<double>(start + info.durationMs / 1000));
    // Type 3: "Watching"; type 2: "Listening".
    return QJsonObject{{QStringLiteral("type"), music ? 2 : 3},
                       {QStringLiteral("details"), clipped(title)},
                       {QStringLiteral("state"), clipped(state.join(QStringLiteral(" · ")))},
                       {QStringLiteral("assets"), assets},
                       {QStringLiteral("timestamps"), timestamps},
                       {QStringLiteral("instance"), false}};
  }

} // namespace jfd::integrations
