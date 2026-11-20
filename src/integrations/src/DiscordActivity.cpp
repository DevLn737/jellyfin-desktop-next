#include <jfd/integrations/DiscordActivity.h>

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>

namespace jfd::integrations {

  namespace {
    // Kept at this path on the main branch of the public repository.
    const QString Logo = QStringLiteral(
        "https://raw.githubusercontent.com/DevLn737/jellyfin-desktop-next/main/packaging/icons/jellyfin-desktop-next.png");
    // Discord refuses longer image and link addresses.
    constexpr int MaxUrl = 256;

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

    QString kindName(PresenceInfo::Kind kind) {
      switch (kind) {
      case PresenceInfo::Kind::Episode: return QCoreApplication::translate("DiscordActivity", "Series");
      case PresenceInfo::Kind::Music: return QCoreApplication::translate("DiscordActivity", "Music");
      case PresenceInfo::Kind::LiveTv: return QCoreApplication::translate("DiscordActivity", "TV");
      case PresenceInfo::Kind::Movie: break;
      }
      return QCoreApplication::translate("DiscordActivity", "Movie");
    }

    bool usableUrl(const QUrl& url) {
      return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty() &&
             url.userInfo().isEmpty() && url.toString(QUrl::FullyEncoded).size() <= MaxUrl;
    }

    QString encodedUrl(const QUrl& url) {
      return url.toString(QUrl::FullyEncoded);
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

  QList<PresenceLink> presenceLinks(PresenceInfo::Kind kind, const QHash<QString, QString>& providerIds,
                                    const QString& artist, const QString& track) {
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9-]+$"));
    static const QRegularExpression imdb(QStringLiteral("^tt[0-9]+$"));
    auto id = [&providerIds](const char* key) {
      // Provider names are compared without case: plugins differ.
      for (auto it = providerIds.begin(); it != providerIds.end(); ++it)
        if (it.key().compare(QLatin1String(key), Qt::CaseInsensitive) == 0) return it.value().trimmed();
      return QString();
    };
    QList<PresenceLink> links;
    auto add = [&links](const QString& label, const QString& url) {
      const QUrl parsed(url);
      if (links.size() < 2 && usableUrl(parsed)) links.append({label, parsed});
    };
    if (kind == PresenceInfo::Kind::Music) {
      if (const QString album = id("MusicBrainzAlbum"); identifier.match(album).hasMatch())
        add(QStringLiteral("MusicBrainz"), QStringLiteral("https://musicbrainz.org/release/") + album);
      else if (const QString group = id("MusicBrainzReleaseGroup"); identifier.match(group).hasMatch())
        add(QStringLiteral("MusicBrainz"), QStringLiteral("https://musicbrainz.org/release-group/") + group);
      if (!artist.trimmed().isEmpty() && !track.trimmed().isEmpty()) {
        // Last.fm writes spaces as "+" and escapes the rest.
        auto part = [](const QString& text) {
          return QString::fromLatin1(QUrl::toPercentEncoding(text.simplified(), " ")).replace(QLatin1Char(' '), QLatin1Char('+'));
        };
        add(QStringLiteral("Last.fm"),
            QStringLiteral("https://www.last.fm/music/%1/_/%2").arg(part(artist), part(track)));
      }
      return links;
    }
    if (kind == PresenceInfo::Kind::LiveTv) return links;
    const bool series = kind == PresenceInfo::Kind::Episode;
    if (const QString shikimori = id("Shikimori"); identifier.match(shikimori).hasMatch())
      add(QStringLiteral("Shikimori"), QStringLiteral("https://shikimori.one/animes/") + shikimori);
    if (const QString code = id("Imdb"); imdb.match(code).hasMatch())
      add(QStringLiteral("IMDb"), QStringLiteral("https://www.imdb.com/title/%1/").arg(code));
    if (const QString tmdb = id("Tmdb"); identifier.match(tmdb).hasMatch())
      add(QStringLiteral("TMDB"),
          QStringLiteral("https://www.themoviedb.org/%1/%2").arg(series ? QStringLiteral("tv") : QStringLiteral("movie"), tmdb));
    return links;
  }

  qint64 presenceStart(qint64 previousStart, qint64 positionMs, bool playing, qint64 nowSeconds) {
    return !playing && previousStart > 0 ? previousStart
                                         : std::max<qint64>(1, nowSeconds - std::max<qint64>(0, positionMs) / 1000);
  }

  QString presenceLogoUrl() {
    return Logo;
  }

  QJsonObject buildActivity(const PresenceInfo& info, qint64 start, const PresenceStyle& style) {
    using Kind = PresenceInfo::Kind;
    const bool episode = info.kind == Kind::Episode;
    const bool music = info.kind == Kind::Music;
    const bool live = info.kind == Kind::LiveTv;
    const QString title = (episode && !info.seriesName.trimmed().isEmpty() ? info.seriesName : info.title).simplified();
    if (title.isEmpty()) return {};

    const QString status = info.buffering ? QCoreApplication::translate("DiscordActivity", "Loading") : info.playing ? (music ? QCoreApplication::translate("DiscordActivity", "Listening") : QCoreApplication::translate("DiscordActivity", "Watching")) : QCoreApplication::translate("DiscordActivity", "Paused");
    QStringList state;
    QString details = title;
    QString largeText = title;
    if (!style.showTitle) {
      details = kindName(info.kind);
      largeText = QStringLiteral("Jellyfin Desktop");
    } else if (episode) {
      QStringList number;
      if (info.season) number << QStringLiteral("S%1").arg(*info.season, 2, 10, QLatin1Char('0'));
      if (info.episode) {
        QString e = QStringLiteral("E%1").arg(*info.episode, 2, 10, QLatin1Char('0'));
        if (info.episodeEnd && *info.episodeEnd > *info.episode)
          e += QStringLiteral("–%1").arg(*info.episodeEnd, 2, 10, QLatin1Char('0'));
        number << e;
      }
      QStringList line;
      line << (number.isEmpty() ? QCoreApplication::translate("DiscordActivity", "Series") : number.join(QLatin1Char(' ')));
      // The episode's own name, unless it only repeats the series or the number.
      const QString name = info.title.simplified();
      if (!name.isEmpty() && name != title && !info.seriesName.isEmpty()) line << name;
      state << line.join(QStringLiteral(" · "));
      if (!info.genres.isEmpty()) largeText += QStringLiteral(" · ") + info.genres.mid(0, 2).join(QStringLiteral(", "));
    } else if (music) {
      QStringList line;
      if (!info.artist.trimmed().isEmpty()) line << info.artist;
      if (!info.album.trimmed().isEmpty()) line << info.album;
      state << (line.isEmpty() ? QCoreApplication::translate("DiscordActivity", "Music") : line.join(QStringLiteral(" · ")));
      if (!info.album.trimmed().isEmpty()) largeText = info.album;
    } else if (live) {
      state << (info.program.trimmed().isEmpty() ? QCoreApplication::translate("DiscordActivity", "Live") : info.program);
    } else {
      state << (info.year > 0 ? QCoreApplication::translate("DiscordActivity", "Movie") + QStringLiteral(" · ") + QString::number(info.year) : QCoreApplication::translate("DiscordActivity", "Movie"));
      QStringList more;
      if (info.year > 0) largeText += QStringLiteral(" (%1)").arg(info.year);
      if (!info.genres.isEmpty()) more << info.genres.mid(0, 2).join(QStringLiteral(", "));
      if (info.rating && *info.rating > 0) more << QStringLiteral("★ %1").arg(*info.rating, 0, 'f', 1);
      if (!more.isEmpty()) largeText += QStringLiteral(" · ") + more.join(QStringLiteral(" · "));
    }
    if (info.groupSize > 1 && !music) state << QCoreApplication::translate("DiscordActivity", "Watching together (%1)").arg(info.groupSize);
    // Discord's timer cannot stop: when paused there is no timer, and the
    // position is in the text.
    const bool paused = !info.playing && !info.buffering;
    if (!info.playing) state << (live ? status : status + QStringLiteral(" · ") + clockText(info.positionMs));

    QString image = Logo;
    if (style.showTitle) {
      if (usableUrl(info.externalPoster))
        image = encodedUrl(info.externalPoster);
      else if (info.poster.isValid())
        image = encodedUrl(info.poster);
    }
    const QJsonObject assets{{QStringLiteral("large_image"), image},
                             {QStringLiteral("large_text"), clipped(largeText)},
                             {QStringLiteral("small_image"), Logo},
                             {QStringLiteral("small_text"), QStringLiteral("Jellyfin Desktop · ") + status}};
    QJsonObject activity{{QStringLiteral("type"), music ? 2 : 3},
                         {QStringLiteral("details"), clipped(details)},
                         {QStringLiteral("assets"), assets},
                         {QStringLiteral("instance"), false}};
    const QString stateText = clipped(state.join(QStringLiteral(" · ")));
    if (!stateText.isEmpty()) activity.insert(QStringLiteral("state"), stateText);
    if (!paused) {
      QJsonObject timestamps{{QStringLiteral("start"), static_cast<double>(start)}};
      if (info.durationMs >= 1000 && !live)
        timestamps.insert(QStringLiteral("end"), static_cast<double>(start + info.durationMs / 1000));
      activity.insert(QStringLiteral("timestamps"), timestamps);
    }
    if (style.showTitle) {
      // The member list says "Watching <title>" instead of the app's name.
      activity.insert(QStringLiteral("status_display_type"), 2);
      QJsonArray buttons;
      for (const PresenceLink& link : info.links) {
        if (buttons.size() == 2 || !usableUrl(link.url) || link.label.trimmed().isEmpty()) continue;
        QString label = link.label.simplified();
        // Discord's limit for a button's label.
        while (label.toUtf8().size() > 32) label.chop(label.back().isLowSurrogate() ? 2 : 1);
        buttons.append(QJsonObject{{QStringLiteral("label"), label}, {QStringLiteral("url"), encodedUrl(link.url)}});
      }
      if (!buttons.isEmpty()) {
        activity.insert(QStringLiteral("buttons"), buttons);
        activity.insert(QStringLiteral("details_url"), buttons.first().toObject().value(QStringLiteral("url")));
      }
    }
    return activity;
  }

} // namespace jfd::integrations
