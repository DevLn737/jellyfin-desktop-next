#include <jfd/integrations/NowPlayingItem.h>

#include <jfd/api/JellyfinClient.h>

namespace jfd::integrations {

  namespace {
    // Large enough for the Windows overlay and desktop widgets.
    constexpr int PosterWidth = 512;
  } // namespace

  QString posterItemId(const api::Item& item) {
    if (item.type == QLatin1String("Episode") && !item.seriesId.isEmpty()) return item.seriesId;
    if (item.type == QLatin1String("Audio") && !item.albumId.isEmpty()) return item.albumId;
    return item.id;
  }

  NowPlayingDetails nowPlayingDetails(const api::Item& item, const api::JellyfinClient& client) {
    NowPlayingDetails details;
    details.item = core::ItemId(item.id.toStdString());
    details.year = item.productionYear.value_or(0);
    details.genres = item.genres;
    details.rating = item.communityRating;

    QString posterItem = item.id;
    QString posterTag = item.primaryImageTag;
    if (item.type == QLatin1String("Episode")) {
      details.kind = PresenceInfo::Kind::Episode;
      details.seriesName = item.seriesName;
      details.season = item.parentIndexNumber;
      details.episode = item.indexNumber;
      if (!item.seriesId.isEmpty() && !item.seriesPrimaryImageTag.isEmpty()) {
        posterItem = item.seriesId;
        posterTag = item.seriesPrimaryImageTag;
      }
    }

    if (item.type == QLatin1String("Audio")) {
      details.kind = PresenceInfo::Kind::Music;
      details.artist = item.artists.isEmpty() ? item.albumArtist : item.artists.join(QStringLiteral(", "));
      details.album = item.album;
      // The cover is the album's.
      if (!item.albumId.isEmpty() && !item.albumPrimaryImageTag.isEmpty()) {
        posterItem = item.albumId;
        posterTag = item.albumPrimaryImageTag;
      }
    }

    if (item.type == QLatin1String("TvChannel")) {
      details.kind = PresenceInfo::Kind::LiveTv;
      details.program = item.currentProgramName;
    }

    // An episode's links are its series'; the app fills them in later.
    if (details.kind != PresenceInfo::Kind::Episode)
      details.links = presenceLinks(details.kind, item.providerIds, details.artist, item.name);

    if (!posterTag.isEmpty()) {
      api::ImageRequest poster;
      poster.itemId = posterItem;
      poster.tag = posterTag;
      poster.maxWidth = PosterWidth;
      details.artUrl = client.imageUrl(poster);
      details.publicPoster = publicPosterUrl(client.serverUrl(), posterItem, posterTag);
    }
    return details;
  }

} // namespace jfd::integrations
