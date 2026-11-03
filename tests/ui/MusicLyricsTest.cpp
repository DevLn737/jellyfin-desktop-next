#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/ui/MusicLyrics.h>

#include <QSignalSpy>

// The lyrics on "Now playing" against a fake server: synced lines follow
// the position, plain ones have no current line, a track without lyrics is
// empty (not an error), and a late answer for the track before is dropped.

using namespace jfd;
using jfd::test::waitFor;
using jfd::ui::MusicLyrics;

namespace {
  test::HttpResponse lyrics(const test::HttpRequest& request) {
    if (request.path == QLatin1String("/Audio/synced/Lyrics"))
      return test::json(R"({"Metadata": {"IsSynced": true}, "Lyrics": [
        {"Text": "One", "Start": 10000000}, {"Text": "Two", "Start": 50000000},
        {"Text": "Three", "Start": 90000000}]})");
    if (request.path == QLatin1String("/Audio/plain/Lyrics"))
      return test::json(R"({"Lyrics": [{"Text": "Just words"}, {"Text": "More words"}]})");
    if (request.path == QLatin1String("/Audio/slow/Lyrics")) {
      test::HttpResponse late = test::json(R"({"Lyrics": [{"Text": "Late", "Start": 0}]})");
      late.delayMs = 300;
      return late;
    }
    if (request.path == QLatin1String("/Audio/broken/Lyrics")) return test::json(R"({})", 500);
    return test::json(R"({"status": 404})", 404);
  }

  struct Fixture {
    test::FakeServer server{lyrics};
    api::JellyfinClient client{api::ClientIdentity{}};
    MusicLyrics model{client};

    Fixture() {
      client.setServerUrl(server.url());
      client.setSession(QStringLiteral("token"), QStringLiteral("user"));
    }

    QString text(int row) const { return model.data(model.index(row), MusicLyrics::TextRole).toString(); }
    bool current(int row) const { return model.data(model.index(row), MusicLyrics::IsCurrentRole).toBool(); }
  };
} // namespace

TEST_CASE("Synced lyrics follow the position") {
  Fixture f;
  QSignalSpy lines(&f.model, &MusicLyrics::currentLineChanged);
  f.model.setPosition(6.0);
  f.model.setTrackId(QStringLiteral("synced"));
  CHECK(f.model.loading());
  CHECK_FALSE(f.model.empty());
  REQUIRE(waitFor([&] { return !f.model.loading(); }));
  REQUIRE(f.model.count() == 3);
  CHECK(f.model.synced());
  CHECK(f.text(1) == QStringLiteral("Two"));
  CHECK(f.model.data(f.model.index(1), MusicLyrics::StartRole).toDouble() == doctest::Approx(5.0));
  CHECK(f.model.currentLine() == 1);
  CHECK(f.current(1));
  CHECK_FALSE(f.current(0));

  f.model.setPosition(9.5);
  CHECK(f.model.currentLine() == 2);
  CHECK(f.current(2));
  CHECK_FALSE(f.current(1));
  f.model.setPosition(0.5);
  CHECK(f.model.currentLine() == -1);
  CHECK(lines.count() >= 3);
}

TEST_CASE("Plain lyrics have no current line; no lyrics is not an error") {
  Fixture f;
  f.model.setTrackId(QStringLiteral("plain"));
  REQUIRE(waitFor([&] { return !f.model.loading(); }));
  CHECK(f.model.count() == 2);
  CHECK_FALSE(f.model.synced());
  f.model.setPosition(100.0);
  CHECK(f.model.currentLine() == -1);

  f.model.setTrackId(QStringLiteral("none"));
  // The lines of the track before go at once.
  CHECK(f.model.count() == 0);
  REQUIRE(waitFor([&] { return !f.model.loading(); }));
  CHECK(f.model.empty());
  CHECK(f.model.errorText().isEmpty());

  f.model.setTrackId(QStringLiteral("broken"));
  REQUIRE(waitFor([&] { return !f.model.loading(); }));
  CHECK_FALSE(f.model.empty());
  CHECK_FALSE(f.model.errorText().isEmpty());

  // No track: nothing is asked for.
  const auto asked = f.server.requests().size();
  f.model.setTrackId(QString());
  CHECK_FALSE(f.model.loading());
  CHECK_FALSE(f.model.empty());
  CHECK(f.server.requests().size() == asked);
}

TEST_CASE("A late answer for the track before is dropped") {
  Fixture f;
  f.model.setTrackId(QStringLiteral("slow"));
  f.model.setTrackId(QStringLiteral("plain"));
  REQUIRE(waitFor([&] { return !f.model.loading(); }));
  waitFor([] { return false; }, 500);
  REQUIRE(f.model.count() == 2);
  CHECK(f.text(0) == QStringLiteral("Just words"));
}
