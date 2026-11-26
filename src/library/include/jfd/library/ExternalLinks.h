#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QUrl>

namespace jfd::library {

  // A link from an item page to the same film or series on another site.
  struct ExternalLink {
    // "shikimori", "myanimelist", "anilist", "anidb", "tvdb", "tmdb", "imdb".
    QString site;
    // The site's name as people write it ("Shikimori", "IMDb").
    QString label;
    QUrl url;
  };

  struct ExternalLinkOptions {
    // "io" or "rip": the one Shikimori domain the links use.
    QString shikimoriDomain = QStringLiteral("io");
    // Sites the user turned off.
    QSet<QString> hidden;
  };

  // Every site in the order an anime shows them, then a film or series.
  QStringList externalLinkSites();
  // The links for a movie or series (other types get none), from its
  // ProviderIds; only sites with a usable ID. An anime (Shikimori, MAL,
  // AniList or AniDB known) leads with those, in that order, then TVDB,
  // TMDB and IMDb; at most `limit` links.
  QList<ExternalLink> externalLinks(const QString& itemType, const QHash<QString, QString>& providerIds,
                                    const ExternalLinkOptions& options = {}, int limit = 6);

} // namespace jfd::library
