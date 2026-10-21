#include <jfd/integrations/NowPlayingItem.h>

#include <jfd/api/JellyfinClient.h>

namespace jfd::integrations {

  namespace {
    // Large enough for the Windows overlay and desktop widgets.
    constexpr int PosterWidth = 512;
  } // namespace

  NowPlayingDetails nowPlayingDetails(const api::Item& item, const api::JellyfinClient& client) {
    NowPlayingDetails details;
    details.item = core::ItemId(item.id.toStdString());
    details.year = item.productionYear.value_or(0);

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
