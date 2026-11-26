#include <doctest/doctest.h>

#include <jfd/library/ExternalLinks.h>

#include <QStringList>

#include <string>

using jfd::library::ExternalLink;
using jfd::library::ExternalLinkOptions;
using jfd::library::externalLinks;

namespace {
  // "shikimori,myanimelist", for comparing with a plain string.
  std::string sites(const QList<ExternalLink>& links) {
    QStringList out;
    for (const ExternalLink& link : links) out.append(link.site);
    return out.join(QLatin1Char(',')).toStdString();
  }

  std::string urlOf(const QList<ExternalLink>& links, const char* site) {
    for (const ExternalLink& link : links)
      if (link.site == QLatin1String(site)) return link.url.toString().toStdString();
    return {};
  }

  const QHash<QString, QString> Anime{{QStringLiteral("Shikimori"), QStringLiteral("5114")},
                                      {QStringLiteral("AniList"), QStringLiteral("5114")},
                                      {QStringLiteral("AniDB"), QStringLiteral("6107")},
                                      {QStringLiteral("Tvdb"), QStringLiteral("85249")},
                                      {QStringLiteral("Tmdb"), QStringLiteral("31911")},
                                      {QStringLiteral("Imdb"), QStringLiteral("tt1355642")}};
} // namespace

TEST_CASE("An anime leads with Shikimori, MAL, AniList and AniDB, and stops at six") {
  const QList<ExternalLink> links = externalLinks(QStringLiteral("Series"), Anime);
  CHECK(sites(links) == "shikimori,myanimelist,anilist,anidb,tvdb,tmdb");
  CHECK(urlOf(links, "shikimori") == "https://shikimori.io/animes/5114");
  // Shikimori's numbers are MyAnimeList's.
  CHECK(urlOf(links, "myanimelist") == "https://myanimelist.net/anime/5114");
  CHECK(urlOf(links, "anilist") == "https://anilist.co/anime/5114");
  CHECK(urlOf(links, "anidb") == "https://anidb.net/anime/6107");
  CHECK(urlOf(links, "tvdb") == "https://thetvdb.com/dereferrer/series/85249");
  CHECK(urlOf(links, "tmdb") == "https://www.themoviedb.org/tv/31911");
}

TEST_CASE("The Shikimori domain and the sites turned off follow the settings") {
  ExternalLinkOptions options;
  options.shikimoriDomain = QStringLiteral("rip");
  options.hidden = {QStringLiteral("anilist"), QStringLiteral("tvdb")};
  const QList<ExternalLink> links = externalLinks(QStringLiteral("Series"), Anime, options);
  CHECK(sites(links) == "shikimori,myanimelist,anidb,tmdb,imdb");
  CHECK(urlOf(links, "shikimori") == "https://shikimori.rip/animes/5114");
  CHECK(urlOf(links, "imdb") == "https://www.imdb.com/title/tt1355642/");
}

TEST_CASE("A film gets only the sites it has IDs for; other types get none") {
  const QHash<QString, QString> ids{{QStringLiteral("imdb"), QStringLiteral(" tt0133093 ")},
                                    {QStringLiteral("Tmdb"), QStringLiteral("603")},
                                    {QStringLiteral("Tvdb"), QString()},
                                    {QStringLiteral("AniDB"), QStringLiteral("not a number")}};
  const QList<ExternalLink> links = externalLinks(QStringLiteral("Movie"), ids);
  CHECK(sites(links) == "tmdb,imdb");
  CHECK(urlOf(links, "tmdb") == "https://www.themoviedb.org/movie/603");
  CHECK(urlOf(links, "imdb") == "https://www.imdb.com/title/tt0133093/");
  CHECK(externalLinks(QStringLiteral("Episode"), ids).isEmpty());
  CHECK(externalLinks(QStringLiteral("Season"), Anime).isEmpty());
}

TEST_CASE("A Shikimori ID with a letter has no MyAnimeList link of its own") {
  const QHash<QString, QString> ids{{QStringLiteral("Shikimori"), QStringLiteral("z40028")}};
  const QList<ExternalLink> links = externalLinks(QStringLiteral("Series"), ids);
  CHECK(sites(links) == "shikimori");
  CHECK(urlOf(links, "shikimori") == "https://shikimori.io/animes/z40028");
}
