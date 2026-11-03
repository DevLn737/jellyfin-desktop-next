#include "../support/DoctestQt.h"

#include <jfd/ui/LibraryImageProvider.h>
#include <jfd/ui/LibraryUi.h>

#include <QSignalSpy>

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
  CHECK(ui.routeFor(QStringLiteral("movie:603"), QStringLiteral("SeerrTitle")) ==
        QStringLiteral("seerr-title/movie:603"));
  CHECK(ui.routeFor(QStringLiteral("d"), QStringLiteral("Episode")) == QStringLiteral("item/d"));
  // Music found by a search opens the music screens.
  CHECK(ui.routeFor(QStringLiteral("e"), QStringLiteral("MusicAlbum")) == QStringLiteral("album/e"));
  CHECK(ui.routeFor(QStringLiteral("f"), QStringLiteral("MusicArtist")) == QStringLiteral("artist/f"));
  CHECK(ui.routeFor(QStringLiteral("g"), QStringLiteral("Playlist")) == QStringLiteral("playlist/g"));
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

  CHECK_FALSE(ui.canWatchTogether());
  ui.watchTogether(QStringLiteral("item"), QStringLiteral("Name"), 0);
  CHECK(messages.count() == 2);
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
    if (!available) return false;
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
