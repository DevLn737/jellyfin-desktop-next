#include "../support/DoctestQt.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/integrations/IntegrationsSettings.h>
#include <jfd/integrations/NowPlayingItem.h>
#include <jfd/settings/Settings.h>

#include <QTemporaryDir>
#include <QUrlQuery>

using namespace jfd::integrations;
using jfd::core::ItemId;

TEST_CASE("Discord is off and the update check on until the user changes them") {
  jfd::settings::SettingsRegistry registry;
  REQUIRE(registerIntegrationsSettings(registry));
  CHECK_FALSE(registerIntegrationsSettings(registry));

  QTemporaryDir dir;
  jfd::settings::Settings settings(registry, dir.filePath(QStringLiteral("settings.ini")));
  CHECK(settings.value(keys::Discord) == QVariant(false));
  // The poster shares the server's address with Discord: opt-in.
  CHECK(settings.value(keys::DiscordPoster) == QVariant(false));
  CHECK(settings.value(keys::CheckForUpdates) == QVariant(true));
  CHECK(settings.setValue(keys::Discord, true));
  CHECK(settings.value(keys::Discord) == QVariant(true));
}

TEST_CASE("An episode shows its series, numbers and the series poster") {
  jfd::api::JellyfinClient client(jfd::api::ClientIdentity{});
  client.setServerUrl(QUrl(QStringLiteral("https://media.example.org/jellyfin")));
  client.setSession(QStringLiteral("secret-token"), QStringLiteral("user"));

  jfd::api::Item episode;
  episode.id = QStringLiteral("episode1");
  episode.type = QStringLiteral("Episode");
  episode.seriesId = QStringLiteral("series1");
  episode.seriesName = QStringLiteral("Show");
  episode.parentIndexNumber = 1;
  episode.indexNumber = 2;
  episode.primaryImageTag = QStringLiteral("thumbtag");
  episode.seriesPrimaryImageTag = QStringLiteral("postertag");

  const auto details = nowPlayingDetails(episode, client);
  CHECK(details.item == ItemId("episode1"));
  CHECK(details.kind == PresenceInfo::Kind::Episode);
  CHECK(details.seriesName == QStringLiteral("Show"));
  CHECK(details.season == 1);
  CHECK(details.episode == 2);
  CHECK(details.artUrl.path() == QStringLiteral("/jellyfin/Items/series1/Images/Primary"));
  CHECK(QUrlQuery(details.artUrl).queryItemValue(QStringLiteral("tag")) == QStringLiteral("postertag"));
  // Images need no token; it must never end up in a URL that leaves the app.
  CHECK_FALSE(details.artUrl.toString().contains(QStringLiteral("secret-token")));
  CHECK(details.publicPoster.path() == QStringLiteral("/jellyfin/Items/series1/Images/Primary"));
  CHECK_FALSE(details.hasNext);
}

TEST_CASE("A movie shows its year; without a poster neither image is set") {
  jfd::api::JellyfinClient client(jfd::api::ClientIdentity{});
  client.setServerUrl(QUrl(QStringLiteral("http://192.168.1.10:8096")));

  jfd::api::Item movie;
  movie.id = QStringLiteral("movie1");
  movie.type = QStringLiteral("Movie");
  movie.productionYear = 2016;
  movie.primaryImageTag = QStringLiteral("tag");

  auto details = nowPlayingDetails(movie, client);
  CHECK(details.kind == PresenceInfo::Kind::Movie);
  CHECK(details.year == 2016);
  CHECK(details.artUrl.path() == QStringLiteral("/Items/movie1/Images/Primary"));
  // Discord cannot fetch from a private address.
  CHECK(details.publicPoster.isEmpty());

  movie.primaryImageTag.clear();
  details = nowPlayingDetails(movie, client);
  CHECK(details.artUrl.isEmpty());
  CHECK(details.publicPoster.isEmpty());
}

TEST_CASE("A track shows its artists and album, with the album's cover") {
  jfd::api::JellyfinClient client(jfd::api::ClientIdentity{});
  client.setServerUrl(QUrl(QStringLiteral("https://media.example.org/jellyfin")));

  jfd::api::Item track;
  track.id = QStringLiteral("track1");
  track.type = QStringLiteral("Audio");
  track.name = QStringLiteral("Song");
  track.artists = {QStringLiteral("A"), QStringLiteral("B")};
  track.album = QStringLiteral("Record");
  track.albumId = QStringLiteral("album1");
  track.albumPrimaryImageTag = QStringLiteral("covertag");

  const auto details = nowPlayingDetails(track, client);
  CHECK(details.kind == PresenceInfo::Kind::Music);
  CHECK(details.artist == QStringLiteral("A, B"));
  CHECK(details.album == QStringLiteral("Record"));
  CHECK(details.artUrl.path() == QStringLiteral("/jellyfin/Items/album1/Images/Primary"));
  CHECK(QUrlQuery(details.artUrl).queryItemValue(QStringLiteral("tag")) == QStringLiteral("covertag"));
}

TEST_CASE("Discord shows everything until the user hides something") {
  jfd::settings::SettingsRegistry registry;
  REQUIRE(registerIntegrationsSettings(registry));
  QTemporaryDir dir;
  jfd::settings::Settings settings(registry, dir.filePath(QStringLiteral("settings.ini")));
  CHECK(settings.value(keys::DiscordHidden) == QVariant(false));
  CHECK(settings.value(keys::DiscordDetail) == QVariant(QStringLiteral("full")));
  for (const QLatin1String kind : {keys::DiscordMovies, keys::DiscordSeries, keys::DiscordMusic, keys::DiscordLiveTv})
    CHECK(settings.value(kind) == QVariant(true));
  CHECK(settings.value(keys::DiscordHiddenLibraries).toString().isEmpty());
  CHECK(settings.setValue(keys::DiscordDetail, QStringLiteral("kind")));
  CHECK(settings.setValue(keys::DiscordHiddenLibraries, QStringLiteral("a,b")));
}

TEST_CASE("A movie's links come from its ids; a channel shows what is on") {
  jfd::api::JellyfinClient client(jfd::api::ClientIdentity{});
  client.setServerUrl(QUrl(QStringLiteral("https://media.example.org")));

  jfd::api::Item movie;
  movie.id = QStringLiteral("movie1");
  movie.type = QStringLiteral("Movie");
  movie.genres = {QStringLiteral("Drama")};
  movie.communityRating = 8.1;
  movie.providerIds = {{QStringLiteral("Imdb"), QStringLiteral("tt0111161")}};
  auto details = nowPlayingDetails(movie, client);
  REQUIRE(details.links.size() == 1);
  CHECK(details.links[0].label == QStringLiteral("IMDb"));
  CHECK(details.genres == QStringList{QStringLiteral("Drama")});
  CHECK(details.rating == 8.1);
  CHECK(posterItemId(movie) == QStringLiteral("movie1"));

  jfd::api::Item episode;
  episode.id = QStringLiteral("episode1");
  episode.type = QStringLiteral("Episode");
  episode.seriesId = QStringLiteral("series1");
  episode.providerIds = movie.providerIds;
  // An episode's links are its series', filled in later.
  CHECK(nowPlayingDetails(episode, client).links.isEmpty());
  CHECK(posterItemId(episode) == QStringLiteral("series1"));

  jfd::api::Item channel;
  channel.id = QStringLiteral("channel1");
  channel.type = QStringLiteral("TvChannel");
  channel.name = QStringLiteral("Channel One");
  channel.currentProgramName = QStringLiteral("News");
  details = nowPlayingDetails(channel, client);
  CHECK(details.kind == PresenceInfo::Kind::LiveTv);
  CHECK(details.program == QStringLiteral("News"));
}
