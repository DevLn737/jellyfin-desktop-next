#include "../support/DoctestQt.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/LibraryModel.h>
#include <jfd/library/LibraryUpdates.h>
#include <jfd/ui/LibraryImageProvider.h>
#include <jfd/ui/LibraryUi.h>

#include <QJsonObject>
#include <QSignalSpy>

#include <memory>

using namespace jfd::ui;

TEST_CASE("Over the texture budget new images come at half, then a quarter of their size") {
  TextureBudget budget(1000);
  CHECK(budget.allowedSize(QSize(400, 600)) == QSize(400, 600));
  budget.add(1000);
  CHECK(budget.allowedSize(QSize(400, 600)) == QSize(200, 300));
  budget.add(500);
  CHECK(budget.allowedSize(QSize(400, 600)) == QSize(100, 150));
  budget.remove(1500);
  CHECK(budget.used() == 0);
  CHECK(budget.allowedSize(QSize(400, 600)) == QSize(400, 600));
}

TEST_CASE("Server image URLs become image provider sources") {
  const QUrl url(QStringLiteral("http://host:8096/Items/abc/Images/Primary?fillWidth=320&tag=x"));
  const QString source = LibraryImageProvider::sourceFor(url);
  REQUIRE(source.startsWith(QStringLiteral("image://jfd/")));
  // The provider gets everything after the provider id, still encoded.
  const QString id = source.mid(QStringLiteral("image://jfd/").size());
  CHECK_FALSE(id.contains(QLatin1Char('/')));
  CHECK(QUrl(QUrl::fromPercentEncoding(id.toLatin1())) == url);
  CHECK(LibraryImageProvider::sourceFor(QUrl()).isEmpty());
}

TEST_CASE("Cards lead to a library or an item page") {
  LibraryUi ui;
  CHECK(ui.routeFor(QStringLiteral("a"), QStringLiteral("CollectionFolder")) ==
        QStringLiteral("library/a"));
  CHECK(ui.routeFor(QStringLiteral("b"), QStringLiteral("BoxSet")) ==
        QStringLiteral("collection/b"));
  CHECK(ui.routeFor(QStringLiteral("c"), QStringLiteral("Series")) == QStringLiteral("item/c"));
  CHECK(ui.routeFor(QStringLiteral("d"), QStringLiteral("Episode")) == QStringLiteral("item/d"));
  // Music found by a search opens the music screens.
  CHECK(ui.routeFor(QStringLiteral("e"), QStringLiteral("MusicAlbum")) ==
        QStringLiteral("album/e"));
  CHECK(ui.routeFor(QStringLiteral("f"), QStringLiteral("MusicArtist")) ==
        QStringLiteral("artist/f"));
  CHECK(ui.routeFor(QStringLiteral("g"), QStringLiteral("Playlist")) ==
        QStringLiteral("playlist/g"));
  // A person lists their films and series.
  CHECK(ui.routeFor(QStringLiteral("h"), QStringLiteral("Person")) == QStringLiteral("person/h"));
}

TEST_CASE("Run times read as hours and minutes") {
  LibraryUi ui;
  constexpr qint64 minute = 600'000'000;
  CHECK(ui.formatRunTime(0).isEmpty());
  CHECK(ui.formatRunTime(45 * minute) == QStringLiteral("45 min"));
  CHECK(ui.formatRunTime(120 * minute) == QStringLiteral("2 h"));
  CHECK(ui.formatRunTime(112 * minute) == QStringLiteral("1 h 52 min"));
}

TEST_CASE("Play goes to the player's handler, or says it is not available") {
  LibraryUi ui;
  QSignalSpy messages(&ui, &LibraryUi::message);
  ui.play(QStringLiteral("item"), 10);
  CHECK(messages.count() == 1);

  QString played;
  qint64 at = -1;
  ui.setPlayHandler([&](const QString& id, qint64 ticks) {
    played = id;
    at = ticks;
  });
  CHECK(ui.canPlay());
  ui.play(QStringLiteral("item"), 10);
  CHECK(played == QStringLiteral("item"));
  CHECK(at == 10);
  CHECK(messages.count() == 1);
}

TEST_CASE("Without services the screens get no models") {
  LibraryUi ui;
  CHECK(ui.createLibrary(QStringLiteral("x"), nullptr) == nullptr);
  CHECK(ui.createDetails(QStringLiteral("x"), nullptr) == nullptr);
  CHECK(ui.libraryName(QStringLiteral("x")).isEmpty());
}

TEST_CASE("A track goes to the music player, asking first while a video plays") {
  LibraryUi ui;
  QSignalSpy messages(&ui, &LibraryUi::message);
  QSignalSpy confirm(&ui, &LibraryUi::trackConfirmationNeeded);
  CHECK_FALSE(ui.canPlayTracks());
  ui.playTrack(QStringLiteral("t"), QStringLiteral("a"));
  CHECK(messages.count() == 1);

  QStringList played;
  bool video = false;
  bool available = true;
  LibraryUi::TrackHandler handler;
  handler.play = [&](const QString& track, const QString& album) {
    if (!available)
      return false;
    played << track + QLatin1Char('@') + album;
    return true;
  };
  handler.stopsVideo = [&] { return video; };
  ui.setTrackHandler(std::move(handler));
  CHECK(ui.canPlayTracks());

  ui.playTrack(QStringLiteral("t"), QStringLiteral("a"));
  CHECK(played == QStringList{QStringLiteral("t@a")});

  video = true;
  ui.playTrack(QStringLiteral("u"), QStringLiteral("b"));
  REQUIRE(confirm.count() == 1);
  CHECK(confirm[0][0].toString() == QStringLiteral("u"));
  CHECK(confirm[0][1].toString() == QStringLiteral("b"));
  CHECK(played.size() == 1);
  ui.playTrack(QStringLiteral("u"), QStringLiteral("b"), true);
  CHECK(played.last() == QStringLiteral("u@b"));

  // In a group music cannot play: the toast says why.
  video = false;
  available = false;
  ui.playTrack(QStringLiteral("v"), QString());
  CHECK(played.size() == 2);
  CHECK(messages.count() == 2);
}

TEST_CASE("Live TV: a channel leads to its page and plays on this device only") {
  LibraryUi ui;
  CHECK(ui.routeFor(QStringLiteral("ch"), QStringLiteral("TvChannel")) ==
        QStringLiteral("channel/ch"));
  CHECK(ui.liveTv() == nullptr);
  // Without a client there is no guide or channel page to make.
  CHECK(ui.createGuide(nullptr) == nullptr);
  CHECK(ui.createSchedule(QStringLiteral("ch"), nullptr) == nullptr);

  QSignalSpy messages(&ui, &LibraryUi::message);
  ui.playChannel(QStringLiteral("ch"));
  CHECK(messages.count() == 1);

  // Not through Play, which a group would take over.
  QString played;
  ui.setPlayHandler([&](const QString&, qint64) { FAIL("Play is for files"); });
  ui.setChannelHandler([&](const QString& id) { played = id; });
  CHECK(ui.canPlayChannels());
  ui.playChannel(QStringLiteral("ch"));
  CHECK(played == QStringLiteral("ch"));
  ui.playChannel(QString());
  CHECK(played == QStringLiteral("ch"));
  CHECK(messages.count() == 1);
}

TEST_CASE("A library's sort is kept: an older page of it neither resets it nor keeps an old one") {
  jfd::api::JellyfinClient client{jfd::api::ClientIdentity{}};
  jfd::library::LibraryUpdates updates;
  QJsonObject store;
  int writes = 0;
  LibraryUi ui;
  LibraryUi::Services services;
  services.client = &client;
  services.updates = &updates;
  services.readFilters = [&] { return store; };
  services.writeFilters = [&](const QJsonObject& all) {
    store = all;
    ++writes;
  };
  ui.attach(services);
  const QString id = QStringLiteral("lib1");
  auto sortOf = [&] {
    return store.value(id).toObject().value(QStringLiteral("sortBy")).toString();
  };

  // A page of the library, then another one over it (the first stays in
  // the back history).
  std::unique_ptr<jfd::library::LibraryModel> older(ui.createLibrary(id, nullptr));
  std::unique_ptr<jfd::library::LibraryModel> newer(ui.createLibrary(id, nullptr));
  REQUIRE(older != nullptr);
  REQUIRE(newer != nullptr);
  const int opened = writes;

  // Sorted on the newer page: saved, and the older page takes it.
  newer->setSortBy(QStringLiteral("DateCreated"));
  newer->setDescending(true);
  CHECK(sortOf() == QStringLiteral("DateCreated"));
  CHECK(older->sortBy() == QStringLiteral("DateCreated"));
  CHECK(older->descending());

  // The server's library changes, or the connection comes back: the pages
  // load again and nothing is saved.
  const int sorted = writes;
  CHECK(sorted > opened);
  Q_EMIT updates.resynchronize();
  Q_EMIT updates.libraryChanged({QStringLiteral("new")}, {}, {});
  CHECK(writes == sorted);
  CHECK(sortOf() == QStringLiteral("DateCreated"));

  // A change on the older page is saved too, with the sort it took.
  older->setPlayedFilter(jfd::library::LibraryModel::PlayedFilter::Unplayed);
  CHECK(sortOf() == QStringLiteral("DateCreated"));
  CHECK(store.value(id).toObject().value(QStringLiteral("played")).toString() ==
        QStringLiteral("unplayed"));
  CHECK(newer->playedFilter() == jfd::library::LibraryModel::PlayedFilter::Unplayed);

  // A new page opens as it was left.
  std::unique_ptr<jfd::library::LibraryModel> again(ui.createLibrary(id, nullptr));
  CHECK(again->sortBy() == QStringLiteral("DateCreated"));
}
