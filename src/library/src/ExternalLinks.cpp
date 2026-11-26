#include <jfd/library/ExternalLinks.h>

#include <QRegularExpression>
#include <QStringList>

namespace jfd::library {

  namespace {
    // The ID under any of `keys`; plugins differ in how they spell them.
    QString idOf(const QHash<QString, QString>& providerIds, std::initializer_list<const char*> keys) {
      for (const char* key : keys)
        for (auto it = providerIds.begin(); it != providerIds.end(); ++it)
          if (it.key().compare(QLatin1String(key), Qt::CaseInsensitive) == 0) {
            const QString value = it.value().trimmed();
            if (!value.isEmpty()) return value;
          }
      return {};
    }

    bool matches(const QString& id, const char* pattern) {
      return QRegularExpression(QRegularExpression::anchoredPattern(QLatin1String(pattern))).match(id).hasMatch();
    }
  } // namespace

  QStringList externalLinkSites() {
    return {QStringLiteral("shikimori"), QStringLiteral("myanimelist"), QStringLiteral("anilist"),
            QStringLiteral("anidb"),     QStringLiteral("tvdb"),        QStringLiteral("tmdb"),
            QStringLiteral("imdb")};
  }

  QList<ExternalLink> externalLinks(const QString& itemType, const QHash<QString, QString>& providerIds,
                                    const ExternalLinkOptions& options, int limit) {
    QList<ExternalLink> links;
    const bool series = itemType == QLatin1String("Series");
    if (!series && itemType != QLatin1String("Movie")) return links;

    // Shikimori writes some titles with a letter in front ("z12345"); the
    // plain number is the MyAnimeList ID as well.
    const QString shikimori = idOf(providerIds, {"Shikimori"});
    QString mal = idOf(providerIds, {"MyAnimeList", "Mal"});
    if (mal.isEmpty() && matches(shikimori, "[0-9]+")) mal = shikimori;
    const QString anilist = idOf(providerIds, {"AniList"});
    const QString anidb = idOf(providerIds, {"AniDB"});
    const QString tvdb = idOf(providerIds, {"Tvdb"});
    const QString tmdb = idOf(providerIds, {"Tmdb"});
    const QString imdb = idOf(providerIds, {"Imdb"});

    auto add = [&](const char* site, const char* label, bool usable, const QString& url) {
      if (!usable || links.size() >= limit || options.hidden.contains(QLatin1String(site))) return;
      links.append({QLatin1String(site), QLatin1String(label), QUrl(url)});
    };
    const QString domain = options.shikimoriDomain == QLatin1String("rip") ? QStringLiteral("rip")
                                                                           : QStringLiteral("io");
    add("shikimori", "Shikimori", matches(shikimori, "[A-Za-z]?[0-9]+"),
        QStringLiteral("https://shikimori.%1/animes/%2").arg(domain, shikimori));
    add("myanimelist", "MyAnimeList", matches(mal, "[0-9]+"), QStringLiteral("https://myanimelist.net/anime/") + mal);
    add("anilist", "AniList", matches(anilist, "[0-9]+"), QStringLiteral("https://anilist.co/anime/") + anilist);
    add("anidb", "AniDB", matches(anidb, "[0-9]+"), QStringLiteral("https://anidb.net/anime/") + anidb);
    add("tvdb", "TVDB", matches(tvdb, "[0-9]+"),
        QStringLiteral("https://thetvdb.com/dereferrer/%1/%2").arg(series ? QStringLiteral("series") : QStringLiteral("movie"), tvdb));
    add("tmdb", "TMDB", matches(tmdb, "[0-9]+"),
        QStringLiteral("https://www.themoviedb.org/%1/%2").arg(series ? QStringLiteral("tv") : QStringLiteral("movie"), tmdb));
    add("imdb", "IMDb", matches(imdb, "tt[0-9]+"), QStringLiteral("https://www.imdb.com/title/%1/").arg(imdb));
    return links;
  }

} // namespace jfd::library
