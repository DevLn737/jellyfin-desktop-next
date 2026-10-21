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
