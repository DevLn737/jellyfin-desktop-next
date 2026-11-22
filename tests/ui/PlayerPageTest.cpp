#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/ShowcaseLook.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/playback/StreamPlan.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/GroupSession.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/PlayerModel.h>
#include <jfd/ui/PlayerSession.h>

#include <QDateTime>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QStyleHints>
#include <QTest>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QSet>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <cmath>
#include <functional>
#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_GroupPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_PlayerPlugin)

// The player page on the session runtime with a virtual backend: no server
// and no mpv. The stream a server would resolve is handed in directly.

using namespace jfd;
using jfd::test::waitFor;

namespace {

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  api::MediaStream stream(int index, const char* type, bool external = false) {
    api::MediaStream s;
    s.index = index;
    s.type = QString::fromLatin1(type);
    s.title = QStringLiteral("%1 %2").arg(s.type).arg(index);
    s.deliveryMethod = external ? QStringLiteral("External") : QString();
    s.isExternal = external;
    return s;
  }

  struct Fixture {
    api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"),
                                                    QStringLiteral("CI"), QStringLiteral("jfd-player-page")}};
    api::SessionSocket socket{client};
    testing::VirtualBackend* backend = nullptr;
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<session::GroupModel> group;
    std::unique_ptr<ui::PlayerModel> player;
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow window;
    QQuickItem* page = nullptr;

    Fixture() {
      session::SessionRuntime::Settings settings;
      settings.deviceProfile = api::buildDeviceProfile({});
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [this](const core::Clock& clock, session::SessionRuntime::Post post) {
            auto made = std::make_unique<testing::VirtualBackend>(clock, std::move(post));
            backend = made.get();
            return made;
          },
          settings);
      group = std::make_unique<session::GroupModel>(*runtime, client, socket);
      ui::GroupSession::setInstance(group.get());
      player = std::make_unique<ui::PlayerModel>(*runtime, client, nullptr);
      ui::PlayerSession::setInstance(player.get());

      QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Player/PlayerPage.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      page = qobject_cast<QQuickItem*>(object.get());
      REQUIRE(page != nullptr);
      window.resize(1280, 720);
      page->setParentItem(window.contentItem());
      page->setSize(QSizeF(1280, 720));
      window.show();
    }

    // What the launcher emits once the server resolved the item, then the
    // intent it posts.
    void open(bool live = false, const QString& itemId = QStringLiteral("episode"),
              api::PlayMethod method = api::PlayMethod::DirectPlay, const QString& seriesId = {},
              const QString& title = QStringLiteral("Episode")) {
      session::PlaybackLauncher::Request request;
      request.itemId = itemId;
      api::MediaSource source;
      source.id = QStringLiteral("source");
      source.defaultAudioStreamIndex = 2;
      source.mediaStreams = {stream(0, "Subtitle", true), stream(1, "Video"), stream(2, "Audio"),
                             stream(3, "Audio"), stream(4, "Subtitle")};
      api::PlaybackStream played;
      played.url = QUrl(QStringLiteral("https://srv/Videos/episode/stream"));
      played.method = method;
      if (method == api::PlayMethod::Transcode)
        source.transcodingUrl = QStringLiteral(
            "/videos/episode/master.m3u8?MediaSourceId=source&TranscodeReasons=VideoCodecNotSupported%2CSubtitleCodecNotSupported");
      played.itemId = request.itemId;
      played.mediaSourceId = source.id;
      if (live) {
        // A Live TV channel (PlaybackLauncher marks it from the source).
        played.isLive = true;
        played.liveStreamId = QStringLiteral("live-1");
        source.liveStreamId = played.liveStreamId;
        source.isInfiniteStream = true;
      }
      api::ExternalTrack subtitle;
      subtitle.streamIndex = 0;
      subtitle.url = QUrl(QStringLiteral("https://srv/Subtitles/0/Stream.srt"));
      played.externalSubtitles = {subtitle};
      api::Item item;
      item.id = request.itemId;
      if (!seriesId.isEmpty()) {
        item.type = QStringLiteral("Episode");
        item.seriesId = seriesId;
      }
      Q_EMIT runtime->launcher().resolved(request, item, source, played);
      core::MediaRequest media = playback::mediaRequest(played, false, core::MediaTicks(), std::nullopt, false);
      media.title = title.toStdString();
      runtime->post(core::intent::Open{media});
    }
  };

} // namespace

TEST_CASE("The player page plays, pauses, seeks and lists the file's tracks") {
  Fixture f;
  REQUIRE(find(f.page, "timeline") != nullptr);
  REQUIRE(find(f.page, "playPause") != nullptr);
  CHECK_FALSE(f.player->active());

  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  CHECK(f.player->title() == QStringLiteral("Episode"));
  CHECK(f.player->duration() == doctest::Approx(600.0));
  CHECK(f.player->audioTracks().size() == 2);
  CHECK(f.player->subtitleTracks().size() == 2);
  CHECK(f.player->audioIndex() == 2);
  CHECK(f.player->subtitleIndex() == -1);

  f.player->togglePause();
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  f.player->seek(300);
  REQUIRE(waitFor([&] { return f.player->position() > 299 && f.player->position() < 301; }, 2000));
  f.player->togglePause();
  REQUIRE(waitFor([&] { return !f.player->paused(); }, 2000));

  // The external subtitle is added on first selection, then selected.
  f.player->selectSubtitle(0);
  CHECK(f.player->subtitleIndex() == 0);
  REQUIRE(waitFor([&] { return f.backend->addedTracks.size() == 1; }, 2000));
  CHECK(f.backend->addedTracks.front().url == "https://srv/Subtitles/0/Stream.srt");
  f.player->selectSubtitle(-1);
  CHECK(f.player->subtitleIndex() == -1);

  f.player->stop();
  REQUIRE(waitFor([&] { return !f.player->active() || f.player->ended(); }, 2000));
}

TEST_CASE("Chapters mark the seek bar and name the part; one menu holds audio and subtitles") {
  // The item's chapters and an intro from 0 to 60 s.
  test::FakeServer server([](const test::HttpRequest& r) {
    const auto json = [](const char* body) { return test::HttpResponse{200, QByteArray(body), "application/json", 0}; };
    if (r.path == QStringLiteral("/Items/episode"))
      return json(R"({"Id": "episode", "Name": "Episode", "Chapters": [
        {"StartPositionTicks": 0, "Name": "Opening"},
        {"StartPositionTicks": 3000000000, "Name": "Part 1"},
        {"StartPositionTicks": 1800000000, "Name": "Middle"}]})");
    if (r.path == QStringLiteral("/MediaSegments/episode"))
      return json(R"({"Items": [{"Id": "s1", "ItemId": "episode", "Type": "Intro",
        "StartTicks": 0, "EndTicks": 600000000}]})");
    return test::HttpResponse{404, {}, {}, 0};
  });
  Fixture f;
  f.client.setServerUrl(server.url());
  f.client.setSession(QStringLiteral("token"), QStringLiteral("user"));
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading(); }, 5000));
  REQUIRE(waitFor([&] { return f.player->chapters().size() == 3; }, 5000));
  // In order of their start, whatever the server's order.
  CHECK(f.player->chapters()[1].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Middle"));
  CHECK(f.player->chapterAt(200) == QStringLiteral("Middle"));
  CHECK(f.player->chapterAt(400) == QStringLiteral("Part 1"));
  CHECK(f.player->chapterAt(10) == QStringLiteral("Opening"));

  // The marks: one per chapter after the first.
  int marks = 0;
  const std::function<void(QQuickItem*)> count = [&](QQuickItem* item) {
    if (item->objectName() == QLatin1String("chapterMark") && item->isVisible()) ++marks;
    for (QQuickItem* child : item->childItems()) count(child);
  };
  f.page->setProperty("osdVisible", true);
  REQUIRE(waitFor([&] { marks = 0; count(f.page); return marks == 2; }, 3000));

  // In the intro the line beside the time names it, and Skip shows.
  REQUIRE(waitFor([&] { return f.player->segment() == QStringLiteral("Intro"); }, 5000));
  QQuickItem* part = find(f.page, "partText");
  REQUIRE(part != nullptr);
  CHECK(part->property("text").toString() == QStringLiteral("Intro"));
  CHECK(find(f.page, "skipSegment")->isVisible());
  // JFD_UI_SCREENSHOTS=<dir> keeps a picture of the controls for review.
  const QString shots = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
  const auto shoot = [&](const char* name) {
    if (shots.isEmpty()) return;
    QTest::qWait(300);
    f.window.grabWindow().save(shots + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
  };
  shoot("player-1280-intro");
  // Past it, the chapter.
  f.player->seek(400);
  REQUIRE(waitFor([&] { return part->property("text").toString() == QStringLiteral("Part 1"); }, 3000));
  // The chapter's arrows: back from well into it goes to its start, then
  // to the one before; there is no next after the last.
  QQuickItem* chapterNext = find(f.page, "chapterNext");
  REQUIRE(chapterNext != nullptr);
  CHECK_FALSE(chapterNext->isVisible());
  QMetaObject::invokeMethod(f.page, "stepChapter", Q_ARG(QVariant, -1));
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - 300) < 1; }, 3000));
  QMetaObject::invokeMethod(f.page, "stepChapter", Q_ARG(QVariant, -1));
  REQUIRE(waitFor([&] { return part->property("text").toString() == QStringLiteral("Middle"); }, 3000));
  CHECK(chapterNext->isVisible());
  QMetaObject::invokeMethod(f.page, "stepChapter", Q_ARG(QVariant, 1));
  REQUIRE(waitFor([&] { return part->property("text").toString() == QStringLiteral("Part 1"); }, 3000));
  // A speed that is not normal shows on the speed button.
  QQuickItem* speed = find(f.page, "speedButton");
  REQUIRE(speed != nullptr);
  CHECK(speed->property("text").toString().isEmpty());
  f.player->setRate(1.5);
  REQUIRE(waitFor([&] { return speed->property("text").toString() == QStringLiteral("1.5×"); }, 3000));
  shoot("player-1280-chapter");
  f.player->setRate(1);
  REQUIRE(waitFor([&] { return speed->property("text").toString().isEmpty(); }, 3000));

  // Audio and subtitles in one menu: two audio tracks, Off and two subtitles.
  QMetaObject::invokeMethod(f.page, "toggleMenu", Q_ARG(QVariant, QStringLiteral("subtitles")));
  QQuickItem* menu = nullptr;
  REQUIRE(waitFor([&] { return (menu = find(f.page, "tracksMenu")) != nullptr; }, 2000));
  CHECK(find(menu, "audioTrack_1") != nullptr);
  CHECK(find(menu, "audioTrack_2") == nullptr);
  CHECK(find(menu, "subtitleTrack_2") != nullptr);
  CHECK(find(menu, "subtitleTune") != nullptr);
  shoot("player-1280-tracks");

  // "Size, position and delay" opens the player settings at the subtitles,
  // and a size there is the player's (the application keeps it).
  QMetaObject::invokeMethod(find(menu, "subtitleTune"), "clicked");
  QQuickItem* tune = nullptr;
  REQUIRE(waitFor([&] { return (tune = find(f.page, "tuneMenu")) != nullptr; }, 2000));
  CHECK(tune->property("section").toString() == QStringLiteral("subtitles"));
  QQuickItem* large = find(tune, "subtitleSize42");
  REQUIRE(large != nullptr);
  CHECK(large->isVisible());
  QMetaObject::invokeMethod(large, "clicked");
  CHECK(f.player->subtitleSize() == 42);
  shoot("player-1280-subtitles");
  // Back: the list with the values.
  QMetaObject::invokeMethod(find(tune, "tuneBack"), "clicked");
  CHECK(tune->property("section").toString().isEmpty());
  QQuickItem* subtitlesRow = find(tune, "tuneSubtitles");
  REQUIRE(subtitlesRow != nullptr);
  CHECK(subtitlesRow->isVisible());
  CHECK(subtitlesRow->property("value").toString() == QStringLiteral("Large"));
  CHECK(find(tune, "tuneQuality")->isVisible());
  shoot("player-1280-settings");
  f.player->setSubtitleSize(-1);
}

TEST_CASE("The episodes panel shows each episode's still, the watched check and the progress, by season") {
  // Two seasons: 1x1 watched, 1x2 plays, 2x1 half seen (by position),
  // 2x2 without a still of its own (the series' backdrop).
  test::FakeServer server([](const test::HttpRequest& r) {
    const auto json = [](const char* body) { return test::HttpResponse{200, QByteArray(body), "application/json", 0}; };
    if (r.path == QStringLiteral("/Shows/series/Episodes"))
      return json(R"({"TotalRecordCount": 4, "Items": [
        {"Id": "e11", "Name": "Log", "Type": "Episode", "ParentIndexNumber": 1, "IndexNumber": 1,
         "RunTimeTicks": 28800000000, "ImageTags": {"Primary": "p11"}, "UserData": {"Played": true}},
        {"Id": "episode", "Name": "First storm", "Type": "Episode", "ParentIndexNumber": 1, "IndexNumber": 2,
         "RunTimeTicks": 28800000000, "ImageTags": {"Primary": "p12"}, "UserData": {"Played": false}},
        {"Id": "e21", "Name": "New tenant", "Type": "Episode", "ParentIndexNumber": 2, "IndexNumber": 1,
         "RunTimeTicks": 28800000000, "ImageTags": {"Primary": "p21"},
         "UserData": {"Played": false, "PlaybackPositionTicks": 14400000000}},
        {"Id": "e22", "Name": "Fog", "Type": "Episode", "ParentIndexNumber": 2, "IndexNumber": 2,
         "RunTimeTicks": 28800000000, "ParentBackdropItemId": "series", "ParentBackdropImageTags": ["b"],
         "UserData": {"Played": false}}]})");
    return test::HttpResponse{404, {}, {}, 0};
  });
  Fixture f;
  f.client.setServerUrl(server.url());
  f.client.setSession(QStringLiteral("token"), QStringLiteral("user"));
  f.open(false, QStringLiteral("episode"), api::PlayMethod::DirectPlay, QStringLiteral("series"));
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading(); }, 5000));
  REQUIRE(waitFor([&] { return f.player->episodes().size() == 4; }, 5000));

  const QVariantList list = f.player->episodes();
  const auto entry = [&](int i) { return list[i].toMap(); };
  CHECK(entry(0).value(QStringLiteral("played")).toBool());
  CHECK(entry(1).value(QStringLiteral("current")).toBool());
  CHECK(entry(2).value(QStringLiteral("progress")).toDouble() == doctest::Approx(0.5));
  CHECK(entry(0).value(QStringLiteral("progress")).toDouble() == doctest::Approx(0.0));
  CHECK(entry(0).value(QStringLiteral("runTimeTicks")).toLongLong() == 28800000000LL);
  // The episode's own still; the series' backdrop for the one without.
  const QString still = entry(0).value(QStringLiteral("imageSource")).toString();
  CHECK(still.startsWith(QStringLiteral("image://jfd/")));
  CHECK(still.contains(QStringLiteral("e11")));
  CHECK(still.contains(QStringLiteral("Primary")));
  const QString backdrop = entry(3).value(QStringLiteral("imageSource")).toString();
  CHECK(backdrop.contains(QStringLiteral("series")));
  CHECK(backdrop.contains(QStringLiteral("Backdrop")));

  const auto visible = [&](QQuickItem* root, const char* name) {
    int n = 0;
    const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
      if (item->objectName() == QLatin1String(name) && item->isVisible()) ++n;
      for (QQuickItem* child : item->childItems()) walk(child);
    };
    walk(root);
    return n;
  };

  f.page->setProperty("osdVisible", true);
  QMetaObject::invokeMethod(f.page, "toggleMenu", Q_ARG(QVariant, QStringLiteral("episodes")));
  QQuickItem* tabs = nullptr;
  REQUIRE(waitFor([&] { return (tabs = find(f.page, "episodeSeasonTabs")) != nullptr; }, 2000));
  CHECK(tabs->isVisible());
  QQuickItem* watched = find(f.page, "episodesWatched");
  REQUIRE(watched != nullptr);
  CHECK(watched->property("text").toString() == QStringLiteral("1 of 4 watched"));
  // The playing episode's season opens: its two episodes, one with a check.
  REQUIRE(waitFor([&] { return visible(f.page, "episodeRow") == 2; }, 2000));
  CHECK(visible(f.page, "episodeStill") == 2);
  CHECK(visible(f.page, "episodeWatched") == 1);
  CHECK(visible(f.page, "episodeProgress") == 0);
  const QString shots = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
  if (!shots.isEmpty()) {
    QTest::qWait(300);
    f.window.grabWindow().save(shots + QStringLiteral("/player-1280-episodes.png"));
  }

  // The second season's tab: its episodes, the first half seen.
  QQuickItem* second = nullptr;
  REQUIRE(waitFor([&] { return (second = find(f.page, "seasonTab_2")) != nullptr; }, 2000));
  QMetaObject::invokeMethod(second, "clicked");
  REQUIRE(waitFor([&] { return visible(f.page, "episodeProgress") == 1; }, 2000));
  CHECK(visible(f.page, "episodeRow") == 2);
  CHECK(visible(f.page, "episodeWatched") == 0);
}

TEST_CASE("Delays are rounded to tenths and reset for the next file") {
  Fixture f;
  f.player->setSubtitleDelay(0.34);
  CHECK(f.player->subtitleDelay() == doctest::Approx(0.3));
  f.player->setAudioDelay(-0.25);
  CHECK(f.player->audioDelay() == doctest::Approx(-0.3));
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading(); }, 5000));
  CHECK(f.player->subtitleDelay() == 0);
  CHECK(f.player->audioDelay() == 0);
}

TEST_CASE("Keys seek, change the volume and the delays, and the speed applies alone") {
  Fixture f;
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  f.page->forceActiveFocus();
  const auto press = [&](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent pressEvent(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&f.window, &pressEvent);
    QKeyEvent releaseEvent(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(&f.window, &releaseEvent);
  };

  f.player->togglePause();
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  const double before = f.player->position();
  // One step: 15 s by default (Settings, Playback, Seek step).
  press(Qt::Key_Right);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - (before + 15)) < 0.5; }, 2000));
  press(Qt::Key_Left);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - before) < 0.5; }, 2000));

  CHECK(f.player->volume() == 100);
  press(Qt::Key_Down);
  press(Qt::Key_Down);
  CHECK(f.player->volume() == 90);
  press(Qt::Key_Up);
  CHECK(f.player->volume() == 95);
  press(Qt::Key_M);
  CHECK(f.player->muted());
  press(Qt::Key_Up);
  CHECK_FALSE(f.player->muted());
  CHECK(f.player->volume() == 100);

  press(Qt::Key_L);
  press(Qt::Key_L);
  press(Qt::Key_J);
  CHECK(f.player->subtitleDelay() == doctest::Approx(0.1));
  CHECK(f.player->audioDelay() == doctest::Approx(0));
  press(Qt::Key_J, Qt::ShiftModifier);
  CHECK(f.player->audioDelay() == doctest::Approx(-0.1));
  CHECK(f.player->subtitleDelay() == doctest::Approx(0.1));

  f.player->setRate(1.5);
  REQUIRE(waitFor([&] { return f.player->baseRate() == doctest::Approx(1.5); }, 2000));
}

TEST_CASE("Clicks on the bar leave the keys to the page; a double-click or a click on a hidden bar does not pause") {
  Fixture f;
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  f.page->forceActiveFocus();
  const auto press = [&](int key) {
    QKeyEvent pressEvent(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.window, &pressEvent);
    QKeyEvent releaseEvent(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.window, &releaseEvent);
  };
  const int doubleClick = QGuiApplication::styleHints()->mouseDoubleClickInterval();

  // A click on the mute button mutes, and Space is still play/pause, not
  // that button again.
  QQuickItem* mute = find(f.page, "mute");
  REQUIRE(mute != nullptr);
  const QPoint onMute = mute->mapToScene(QPointF(mute->width() / 2, mute->height() / 2)).toPoint();
  QTest::mouseMove(&f.window, onMute);
  REQUIRE(waitFor([&] { return mute->isVisible() && mute->opacity() > 0.99; }, 2000));
  QTest::mouseClick(&f.window, Qt::LeftButton, Qt::NoModifier, onMute);
  CHECK(f.player->muted());
  press(Qt::Key_Space);
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  CHECK(f.player->muted());
  press(Qt::Key_Space);
  REQUIRE(waitFor([&] { return !f.player->paused(); }, 2000));

  // A double-click on the video toggles fullscreen and nothing else.
  const QPoint middle(640, 300);
  QTest::mouseDClick(&f.window, Qt::LeftButton, Qt::NoModifier, middle);
  CHECK_FALSE(waitFor([&] { return f.player->paused(); }, doubleClick + 300));
  const bool fullscreen = f.window.visibility() == QWindow::FullScreen;
  if (fullscreen) f.window.showNormal();

  // With the bar faded out, a click brings it back without pausing (the
  // pointer is already there: no move shows the bar first).
  QTest::mouseMove(&f.window, middle);
  f.page->setProperty("osdVisible", false);
  QTest::mouseClick(&f.window, Qt::LeftButton, Qt::NoModifier, middle);
  CHECK(f.page->property("osdVisible").toBool());
  CHECK_FALSE(waitFor([&] { return f.player->paused(); }, doubleClick + 300));

  // With the bar shown, a single click pauses once the double-click time
  // has passed.
  QTest::mouseClick(&f.window, Qt::LeftButton, Qt::NoModifier, middle);
  CHECK(waitFor([&] { return f.player->paused(); }, doubleClick + 2000));
}

TEST_CASE("The header shows the clock and when the file ends; the episodes button waits for a list") {
  Fixture f;
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  QQuickItem* info = find(f.page, "timeInfo");
  REQUIRE(info != nullptr);
  REQUIRE(waitFor([&] { return info->isVisible() && info->childItems().size() == 2; }, 2000));
  const auto text = [](QQuickItem* item) { return item->property("text").toString(); };
  const QList<QQuickItem*> lines = info->childItems();
  CHECK_FALSE(text(lines[0]).isEmpty());
  CHECK(text(lines[1]).startsWith(QStringLiteral("Ends at ")));
  // Hours and minutes only: nothing that changes every second.
  const QRegularExpression seconds(QStringLiteral("\\d\\d?[:.]\\d\\d[:.]\\d\\d"));
  CHECK_FALSE(text(lines[0]).contains(seconds));
  CHECK_FALSE(text(lines[1]).contains(seconds));
  // No segments in this file: no segments button, and the menu keys do nothing.
  QQuickItem* segmentsButton = find(f.page, "segmentsButton");
  REQUIRE(segmentsButton != nullptr);
  CHECK_FALSE(segmentsButton->isVisible());
  // No series list for this file: no button.
  QQuickItem* episodes = find(f.page, "episodes");
  REQUIRE(episodes != nullptr);
  CHECK_FALSE(episodes->isVisible());
  // E does nothing then.
  QKeyEvent press(QEvent::KeyPress, Qt::Key_E, Qt::NoModifier);
  QCoreApplication::sendEvent(&f.window, &press);
  CHECK(f.page->property("openMenu").toString().isEmpty());

  if (const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS"); !dir.isEmpty())
    f.window.grabWindow().save(dir + QStringLiteral("/player-header.png"));
}

TEST_CASE("Picture in picture keeps the file playing; its window is small, frameless and on top, and goes back") {
  Fixture f;
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  CHECK_FALSE(f.player->pip());
  QQuickItem* button = find(f.page, "pip");
  REQUIRE(button != nullptr);
  CHECK(button->isVisible());

  QQmlComponent component(&f.engine, QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Player/PipWindow.qml")));
  std::unique_ptr<QObject> made(component.create());
  REQUIRE_MESSAGE(made != nullptr, component.errorString().toStdString());
  auto* pip = qobject_cast<QQuickWindow*>(made.get());
  REQUIRE(pip != nullptr);
  CHECK_FALSE(pip->isVisible());
  CHECK(pip->flags().testFlag(Qt::WindowStaysOnTopHint));
  CHECK(pip->flags().testFlag(Qt::FramelessWindowHint));
  CHECK(pip->width() < 800);

  QSignalSpy left(f.player.get(), &ui::PlayerModel::leftPip);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier);
  QCoreApplication::sendEvent(&f.window, &press);
  CHECK(f.player->pip());
  REQUIRE(waitFor([&] { return pip->isVisible(); }, 2000));
  CHECK(pip->title().startsWith(QStringLiteral("Jellyfin Desktop Next")));
  // Playback goes on in the small window, and its controls work.
  const double before = f.player->position();
  REQUIRE(waitFor([&] { return f.player->position() > before + 0.2; }, 3000));
  CHECK(f.player->active());
  QQuickItem* playPause = find(pip->contentItem(), "pipPlayPause");
  REQUIRE(playPause != nullptr);
  f.player->togglePause();
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  f.player->togglePause();
  REQUIRE(waitFor([&] { return !f.player->paused(); }, 2000));

  // Esc goes back to the player page, and the file still plays.
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(pip, &escape);
  CHECK_FALSE(f.player->pip());
  CHECK(left.count() == 1);
  REQUIRE(waitFor([&] { return !pip->isVisible(); }, 2000));
  CHECK(f.player->active());

  // Stopping from the small window clears it.
  f.player->enterPip();
  CHECK(f.player->pip());
  f.player->stop();
  CHECK_FALSE(f.player->pip());
}

TEST_CASE("A live channel shows no timeline, no end time and cannot be sought") {
  Fixture f;
  f.open(true);
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  CHECK(f.player->live());
  // The backend's duration (what mpv buffered) is not shown as the end.
  CHECK(f.player->duration() == doctest::Approx(0.0));
  CHECK_FALSE(f.page->property("hasEnd").toBool());
  QQuickItem* timeline = find(f.page, "timeline");
  REQUIRE(timeline != nullptr);
  CHECK_FALSE(timeline->isVisible());
  QQuickItem* time = find(f.page, "timeText");
  REQUIRE(time != nullptr);
  CHECK(time->property("text").toString() == QStringLiteral("Live"));
  const double before = f.player->position();
  f.player->seek(before + 120);
  QTest::qWait(200);
  CHECK(f.player->position() < before + 60);
  f.player->stop();
  REQUIRE(waitFor([&] { return !f.player->active() || f.player->ended(); }, 2000));
}

TEST_CASE("A live channel names its programme, and Page Up and Down change the channel") {
  // Three channels; the guide has the second one's programmes.
  test::FakeServer server([](const test::HttpRequest& r) {
    const auto json = [](const char* body) { return test::HttpResponse{200, QByteArray(body), "application/json", 0}; };
    if (r.path == QStringLiteral("/LiveTv/Channels"))
      return json(R"({"Items": [
        {"Id": "ch1", "Name": "Test Channel 1", "ChannelNumber": "1"},
        {"Id": "ch2", "Name": "Test Channel 2", "ChannelNumber": "2",
         "CurrentProgram": {"Id": "p0", "Name": "From the list", "StartDate": "2026-11-10T16:00:00Z",
                            "EndDate": "2026-11-10T16:30:00Z"}},
        {"Id": "ch3", "Name": "Test Channel 3", "ChannelNumber": "3"}], "TotalRecordCount": 3, "StartIndex": 0})");
    if (r.path == QStringLiteral("/LiveTv/Programs") &&
        r.query.queryItemValue(QStringLiteral("channelIds")) == QStringLiteral("ch2"))
      return json(R"({"Items": [
        {"Id": "p1", "ChannelId": "ch2", "Name": "Evening news", "StartDate": "2026-11-10T16:00:00Z",
         "EndDate": "2026-11-10T16:30:00Z"},
        {"Id": "p2", "ChannelId": "ch2", "Name": "Weather", "StartDate": "2026-11-10T16:30:00Z",
         "EndDate": "2026-11-10T16:40:00Z"}], "TotalRecordCount": 2, "StartIndex": 0})");
    if (r.path == QStringLiteral("/LiveTv/Programs")) return json(R"({"Items": [], "TotalRecordCount": 0})");
    // The channel the player tunes to: the lookup stays open, so the switch
    // is still under way when the test looks (a failed one goes back).
    if (r.path.startsWith(QStringLiteral("/Items/ch"))) return test::HttpResponse{200, "{}", "application/json", 60000};
    return test::HttpResponse{404, {}, {}, 0};
  });
  Fixture f;
  f.client.setServerUrl(server.url());
  f.client.setSession(QStringLiteral("token"), QStringLiteral("user"));
  QDateTime clock = QDateTime::fromString(QStringLiteral("2026-11-10T16:15:00Z"), Qt::ISODate);
  f.player->setClock([&clock] { return clock; });

  f.open(true, QStringLiteral("ch2"));
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading(); }, 5000));
  REQUIRE(waitFor([&] { return f.player->programTitle() == QStringLiteral("Evening news"); }, 5000));
  REQUIRE(waitFor([&] { return f.player->canChangeChannel(); }, 5000));
  CHECK(f.player->channelName() == QStringLiteral("Test Channel 2"));
  CHECK(f.player->channelNumber() == QStringLiteral("2"));
  CHECK(f.player->programProgress() == doctest::Approx(0.5));
  CHECK(f.player->programEnd() == QDateTime::fromString(QStringLiteral("2026-11-10T16:30:00Z"), Qt::ISODate));

  QQuickItem* program = find(f.page, "programTitle");
  REQUIRE(program != nullptr);
  REQUIRE(waitFor([&] { return program->isVisible(); }, 2000));
  CHECK(program->property("text").toString().endsWith(QStringLiteral("Evening news")));
  QQuickItem* up = find(f.page, "channelUp");
  QQuickItem* down = find(f.page, "channelDown");
  REQUIRE(up != nullptr);
  REQUIRE(down != nullptr);
  CHECK(up->isVisible());
  CHECK(down->isVisible());
  CHECK_FALSE(find(f.page, "next")->isVisible());

  // The programme moves on with the clock, from the guide already loaded.
  clock = clock.addSecs(20 * 60);
  f.player->updateProgram();
  CHECK(f.player->programTitle() == QStringLiteral("Weather"));

  f.page->forceActiveFocus();
  const auto press = [&](int key) {
    QKeyEvent pressEvent(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.window, &pressEvent);
    QKeyEvent releaseEvent(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.window, &releaseEvent);
  };
  const auto asked = [&](const char* id) {
    for (const test::HttpRequest& r : server.requests())
      if (r.path == QStringLiteral("/Items/%1").arg(QLatin1String(id))) return true;
    return false;
  };
  // Channel up: the next in the list, named at once while it tunes in.
  press(Qt::Key_PageUp);
  CHECK(f.player->channelName() == QStringLiteral("Test Channel 3"));
  CHECK(f.player->programTitle().isEmpty());
  REQUIRE(waitFor([&] { return asked("ch3"); }, 5000));
  // Channel down twice: round the list back past the first.
  press(Qt::Key_PageDown);
  CHECK(f.player->channelName() == QStringLiteral("Test Channel 2"));
  press(Qt::Key_ChannelDown);
  CHECK(f.player->channelName() == QStringLiteral("Test Channel 1"));
  REQUIRE(waitFor([&] { return asked("ch1"); }, 5000));
  press(Qt::Key_ChannelDown);
  CHECK(f.player->channelName() == QStringLiteral("Test Channel 3"));
}

TEST_CASE("Page Up and Down do nothing to a file that is not live") {
  Fixture f;
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  CHECK_FALSE(f.player->canChangeChannel());
  CHECK(f.player->channelName().isEmpty());
  CHECK(f.player->programTitle().isEmpty());
  f.page->forceActiveFocus();
  QKeyEvent press(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
  QCoreApplication::sendEvent(&f.window, &press);
  CHECK(f.player->active());
  CHECK_FALSE(find(f.page, "channelUp")->isVisible());
}

namespace {

  // A key as the gamepad sends it (InputRouter): press and release, both
  // marked when held.
  void tap(QQuickWindow& window, int key, bool repeat = false) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(), repeat);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(), repeat);
    QCoreApplication::sendEvent(&window, &release);
  }

  // Inside `ancestor`, or it.
  bool within(const QQuickItem* item, const QQuickItem* ancestor) {
    for (; item; item = item->parentItem())
      if (item == ancestor) return true;
    return false;
  }

  // The player in TV mode, under the shell's arrow navigation, with two
  // pages on the navigator for Back to leave.
  struct TvFixture : Fixture {
    TvFixture() {
      QQmlComponent component(&engine);
      component.setData(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
Item {
  SpatialNavigation.enabled: true
  Component.onCompleted: Theme.tvMode = true
  Component.onDestruction: Theme.tvMode = false
}
)",
                        QUrl(QStringLiteral("qrc:/tv-player.qml")));
      shell.reset(qobject_cast<QQuickItem*>(component.create()));
      REQUIRE_MESSAGE(shell != nullptr, component.errorString().toStdString());
      shell->setParentItem(window.contentItem());
      shell->setSize(QSizeF(1280, 720));
      page->setParentItem(shell.get());
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      REQUIRE(navigator != nullptr);
      const QUrl placeholder(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml"));
      navigator->setPlaceholderPage(placeholder);
      navigator->registerPage(QStringLiteral("home"), placeholder);
      navigator->registerPage(QStringLiteral("player"), placeholder);
      navigator->reset(QStringLiteral("home"));
      navigator->navigate(QStringLiteral("player"));
    }
    ~TvFixture() {
      page->setParentItem(window.contentItem());
      shell.reset();
    }

    std::unique_ptr<QQuickItem> shell;
    ui::Navigator* navigator = nullptr;
  };

} // namespace

TEST_CASE("TV mode: a held arrow seeks faster with one seek, Up shows the display, Back closes menu, display, player") {
  TvFixture f;
  QQuickWindow& window = f.window;
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  f.player->togglePause();
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  REQUIRE(f.navigator->depth() == 2);
  f.page->forceActiveFocus();

  // Nothing focused on the display: Right seeks one step (15 s) at once.
  const double start = f.player->position();
  tap(window, Qt::Key_Right);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - (start + 15)) < 0.5; }, 2000));

  // Held: 15 s steps, then 30 s; the timeline shows the target, and the one
  // seek comes when the key is let go.
  const double held = f.player->position();
  QQuickItem* timeline = find(f.page, "timeline");
  REQUIRE(timeline != nullptr);
  tap(window, Qt::Key_Right);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - (held + 15)) < 0.5; }, 2000));
  for (int i = 0; i < 12; ++i) tap(window, Qt::Key_Right, true);
  CHECK(timeline->property("value").toDouble() == doctest::Approx(held + 15 + 9 * 15 + 3 * 30).epsilon(0.01));
  CHECK(f.player->position() == doctest::Approx(held + 15).epsilon(0.01));
  QKeyEvent letGo(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &letGo);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - (held + 240)) < 0.5; }, 2000));

  // A gamepad's hold sends no release of its own: the seek comes once the
  // repeats stop.
  const double back = f.player->position();
  tap(window, Qt::Key_Left);
  for (int i = 0; i < 3; ++i) tap(window, Qt::Key_Left, true);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - (back - 60)) < 0.5; }, 3000));

  // Up: the display with the timeline focused; its Left and Right seek too.
  f.page->setProperty("osdVisible", false);
  tap(window, Qt::Key_Up);
  CHECK(f.page->property("osdVisible").toBool());
  CHECK(window.activeFocusItem() == timeline);
  const double onTimeline = f.player->position();
  tap(window, Qt::Key_Left);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - (onTimeline - 15)) < 0.5; }, 2000));
  CHECK(window.activeFocusItem() == timeline);

  // Down: the arrows now move between the display's controls.
  tap(window, Qt::Key_Down);
  QQuickItem* focused = window.activeFocusItem();
  CHECK(focused != timeline);
  CHECK(focused != f.page);
  CHECK(within(focused, f.page));
  CHECK(f.player->volume() == 100);

  // The menus are one row of named buttons; the icon buttons are gone.
  QQuickItem* row = find(f.page, "tvMenus");
  REQUIRE(row != nullptr);
  CHECK(row->isVisible());
  QQuickItem* subtitles = find(f.page, "tvSubtitles");
  QQuickItem* audio = find(f.page, "tvAudio");
  QQuickItem* settings = find(f.page, "tvSettings");
  REQUIRE(subtitles != nullptr);
  REQUIRE(audio != nullptr);
  REQUIRE(settings != nullptr);
  CHECK(subtitles->isVisible());
  CHECK(audio->isVisible());
  CHECK(settings->isVisible());
  CHECK_FALSE(find(f.page, "tvEpisodes")->isVisible());
  CHECK(subtitles->mapToScene(QPointF()).y() == doctest::Approx(settings->mapToScene(QPointF()).y()));
  CHECK_FALSE(find(f.page, "episodes")->isVisible());
  // Every control of the display inside the safe area, reachable.
  const QRectF safe(64, 36, 1280 - 2 * 64, 720 - 2 * 36);
  for (QQuickItem* item : {subtitles, audio, settings, timeline, find(f.page, "playPause")}) {
    CAPTURE(item->objectName());
    CHECK(safe.contains(item->mapRectToScene(QRectF(0, 0, item->width(), item->height()))));
    CHECK(item->activeFocusOnTab());
  }
  // From the timeline every button of the row is reached by arrows.
  QSet<QQuickItem*> reached;
  for (QQuickItem* from : {timeline, find(f.page, "playPause"), subtitles, audio, settings}) {
    for (int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
      from->forceActiveFocus(Qt::TabFocusReason);
      tap(window, key);
      reached.insert(window.activeFocusItem());
    }
  }
  CHECK(reached.contains(subtitles));
  CHECK(reached.contains(audio));
  CHECK(reached.contains(settings));
  CHECK(reached.contains(timeline));

  // Select on Subtitles: its menu, focused inside; Back closes it and goes
  // back to the button.
  subtitles->forceActiveFocus(Qt::TabFocusReason);
  tap(window, Qt::Key_Return);
  CHECK(f.page->property("openMenu").toString() == QLatin1String("subtitles"));
  CHECK(window.activeFocusItem() != subtitles);
  CHECK(within(window.activeFocusItem(), f.page));
  tap(window, Qt::Key_Escape);
  CHECK(f.page->property("openMenu").toString().isEmpty());
  CHECK(window.activeFocusItem() == subtitles);
  CHECK(f.navigator->depth() == 2);

  // Back: the display; Back again: the player.
  tap(window, Qt::Key_Escape);
  CHECK_FALSE(f.page->property("osdVisible").toBool());
  CHECK(window.activeFocusItem() == f.page);
  CHECK(f.navigator->depth() == 2);
  tap(window, Qt::Key_Escape);
  CHECK(f.navigator->depth() == 1);
}

TEST_CASE("TV mode: Select plays and pauses; on a channel the display focuses play and Page Up still changes the channel") {
  test::FakeServer server([](const test::HttpRequest& r) {
    if (r.path == QStringLiteral("/LiveTv/Channels"))
      return test::HttpResponse{200, QByteArray(R"({"Items": [
        {"Id": "ch1", "Name": "Test Channel 1", "ChannelNumber": "1"},
        {"Id": "ch2", "Name": "Test Channel 2", "ChannelNumber": "2"},
        {"Id": "ch3", "Name": "Test Channel 3", "ChannelNumber": "3"}], "TotalRecordCount": 3, "StartIndex": 0})"),
                                "application/json", 0};
    if (r.path == QStringLiteral("/LiveTv/Programs"))
      return test::HttpResponse{200, QByteArray(R"({"Items": [], "TotalRecordCount": 0})"), "application/json", 0};
    if (r.path.startsWith(QStringLiteral("/Items/ch"))) return test::HttpResponse{200, "{}", "application/json", 60000};
    return test::HttpResponse{404, {}, {}, 0};
  });
  TvFixture f;
  QQuickWindow& window = f.window;
  f.client.setServerUrl(server.url());
  f.client.setSession(QStringLiteral("token"), QStringLiteral("user"));
  f.open(true, QStringLiteral("ch2"));
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading(); }, 5000));
  REQUIRE(waitFor([&] { return f.player->canChangeChannel(); }, 5000));
  f.page->forceActiveFocus();

  tap(window, Qt::Key_Return);
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  tap(window, Qt::Key_Return);
  REQUIRE(waitFor([&] { return !f.player->paused(); }, 2000));

  // No timeline on a channel: Up focuses play/pause.
  tap(window, Qt::Key_Up);
  CHECK(window.activeFocusItem() == find(f.page, "playPause"));
  // The gamepad's shoulders (Page Up and Down) change the channel from the
  // display too.
  tap(window, Qt::Key_PageUp);
  CHECK(f.player->channelName() == QStringLiteral("Test Channel 3"));
  tap(window, Qt::Key_PageDown);
  CHECK(f.player->channelName() == QStringLiteral("Test Channel 2"));
}

TEST_CASE("Right-click on the video shows the statistics; I hides them; a transcode names its reasons") {
  Fixture f;
  f.open(false, QStringLiteral("episode"), api::PlayMethod::Transcode);
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  // Without mpv only what the server said.
  const QVariantMap stats = f.player->stats();
  CHECK(stats.value(QStringLiteral("method")).toString() == QStringLiteral("Transcode"));
  CHECK(stats.value(QStringLiteral("transcodeReasons")).toString() ==
        QStringLiteral("VideoCodecNotSupported,SubtitleCodecNotSupported"));

  QQuickItem* overlay = find(f.page, "statsOverlay");
  REQUIRE(overlay != nullptr);
  CHECK_FALSE(f.page->property("statsShown").toBool());
  QObject* menu = f.page->findChild<QObject*>(QStringLiteral("videoContextMenu"));
  REQUIRE(menu != nullptr);

  // A right click opens the video's menu and does not pause.
  const QPoint middle(640, 300);
  QTest::mouseMove(&f.window, middle);
  QTest::mouseClick(&f.window, Qt::RightButton, Qt::NoModifier, middle);
  REQUIRE(waitFor([&] { return menu->property("visible").toBool(); }, 2000));
  CHECK(menu->property("entries").toStringList() == QStringList{QStringLiteral("Stats for nerds")});
  QMetaObject::invokeMethod(menu, "close");
  QMetaObject::invokeMethod(menu, "activated", Q_ARG(int, 0));
  REQUIRE(waitFor([&] { return f.page->property("statsShown").toBool() && find(f.page, "statsGrid") != nullptr; }, 2000));
  CHECK_FALSE(f.player->paused());

  // The lines name the play method and its reasons.
  QStringList values;
  const std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
    if (item->objectName() == QLatin1String("statValue")) values << item->property("text").toString();
    for (QQuickItem* child : item->childItems()) collect(child);
  };
  REQUIRE(waitFor([&] {
    values.clear();
    collect(overlay);
    return !values.isEmpty();
  }, 2000));
  CHECK(values.contains(QStringLiteral("Transcoding on the server")));
  CHECK(values.contains(QStringLiteral("VideoCodecNotSupported, SubtitleCodecNotSupported")));
  if (const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS"); !dir.isEmpty()) {
    QTest::qWait(300);
    f.window.grabWindow().save(dir + QStringLiteral("/player-stats.png"));
  }

  // I hides them and shows them again.
  f.page->forceActiveFocus();
  const auto press = [&](int key) {
    QKeyEvent pressEvent(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.window, &pressEvent);
    QKeyEvent releaseEvent(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&f.window, &releaseEvent);
  };
  press(Qt::Key_I);
  CHECK_FALSE(f.page->property("statsShown").toBool());
  CHECK(find(f.page, "statsGrid") == nullptr);
  press(Qt::Key_I);
  CHECK(f.page->property("statsShown").toBool());
  CHECK(menu->property("entries").toStringList() == QStringList{QStringLiteral("Hide stats for nerds")});
}

// The player as the README shows it: a still in place of the video, the
// application's font, Russian, the display over an episode of a series.
// Saved only with JFD_UI_SCREENSHOTS=<dir> (readme-player-1600.png).
TEST_CASE("The display over a series episode, in Russian, as the README shows it") {
  test::FakeServer server([](const test::HttpRequest& r) {
    const auto json = [](const char* body) { return test::HttpResponse{200, QByteArray(body), "application/json", 0}; };
    if (r.path == QStringLiteral("/Items/episode"))
      return json(R"({"Id": "episode", "Name": "Новый жилец", "Chapters": [
        {"StartPositionTicks": 0, "Name": "Пролог"},
        {"StartPositionTicks": 1200000000, "Name": "Шторм"},
        {"StartPositionTicks": 3600000000, "Name": "Берег"}]})");
    if (r.path == QStringLiteral("/Shows/series/Episodes"))
      return json(R"({"TotalRecordCount": 3, "Items": [
        {"Id": "e1", "Name": "Журнал", "Type": "Episode", "ParentIndexNumber": 1, "IndexNumber": 1,
         "RunTimeTicks": 28800000000, "UserData": {"Played": true}},
        {"Id": "e2", "Name": "Первый шторм", "Type": "Episode", "ParentIndexNumber": 1, "IndexNumber": 2,
         "RunTimeTicks": 28800000000, "UserData": {"Played": true}},
        {"Id": "episode", "Name": "Новый жилец", "Type": "Episode", "ParentIndexNumber": 1, "IndexNumber": 3,
         "RunTimeTicks": 28800000000, "UserData": {"Played": false}}]})");
    if (r.path == QStringLiteral("/MediaSegments/episode")) return json(R"({"Items": []})");
    return test::HttpResponse{404, {}, {}, 0};
  });

  test::ShowcaseLook look;
  Fixture f;
  test::ShowcaseLook::apply(f.engine);
  f.client.setServerUrl(server.url());
  f.client.setSession(QStringLiteral("token"), QStringLiteral("user"));
  f.open(false, QStringLiteral("episode"), api::PlayMethod::DirectPlay, QStringLiteral("series"),
         QStringLiteral("Хроники маяка – Новый жилец"));
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading(); }, 5000));
  REQUIRE(waitFor([&] { return f.player->episodes().size() == 3; }, 5000));
  REQUIRE(waitFor([&] { return f.player->chapters().size() == 3; }, 5000));
  CHECK(f.page->property("mainTitle").toString() == QStringLiteral("Хроники маяка"));
  CHECK(f.page->property("episodeLine").toString().contains(QStringLiteral("«Новый жилец»")));

  const QString shots = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
  if (shots.isEmpty()) return;
  // A still under the page in place of the video: the page's black fill
  // hidden, a night sea scene behind it.
  constexpr int Width = 1600;
  constexpr int Height = 900;
  for (QQuickItem* child : f.page->childItems())
    if (child->property("color").value<QColor>() == QColor(Qt::black) && child->width() == f.page->width())
      child->setVisible(false);
  QImage still(Width, Height, QImage::Format_RGB32);
  {
    QPainter p(&still);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, Height);
    sky.setColorAt(0, QColor::fromHsv(215, 120, 60));
    sky.setColorAt(0.55, QColor::fromHsv(200, 90, 140));
    sky.setColorAt(1, QColor::fromHsv(190, 70, 120));
    p.fillRect(still.rect(), sky);
    QRadialGradient glow(Width * 0.68, Height * 0.3, Height * 0.5);
    glow.setColorAt(0, QColor(255, 240, 220, 210));
    glow.setColorAt(0.12, QColor(255, 230, 200, 120));
    glow.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillRect(still.rect(), glow);
    for (int layer = 0; layer < 4; ++layer) {
      QPainterPath hills;
      const double base = Height * (0.58 + 0.1 * layer);
      hills.moveTo(0, Height);
      hills.lineTo(0, base);
      for (int x = 0; x <= Width; x += Width / 64) {
        const double y = base - Height * 0.06 * std::sin(1.3 * layer + x * (2.0 + layer) / Width * 3.14) -
                         Height * 0.02 * std::sin(2.1 * layer + x * 9.0 / Width);
        hills.lineTo(x, y);
      }
      hills.lineTo(Width, Height);
      hills.closeSubpath();
      p.fillPath(hills, QColor::fromHsv((205 + 8 * layer) % 360, 110 + 20 * layer, 100 - 20 * layer));
    }
    // The lighthouse on the nearest hill, its beam across the sky.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 26, 36));
    p.drawPolygon(QPolygonF{{QPointF(300, 640), QPointF(318, 470), QPointF(342, 470), QPointF(360, 640)}});
    p.setBrush(QColor(255, 236, 190));
    p.drawRect(QRectF(314, 448, 32, 22));
    QLinearGradient beam(330, 459, 1100, 360);
    beam.setColorAt(0, QColor(255, 240, 200, 150));
    beam.setColorAt(1, QColor(255, 240, 200, 0));
    p.setBrush(beam);
    p.drawPolygon(QPolygonF{{QPointF(330, 452), QPointF(1150, 300), QPointF(1150, 440), QPointF(330, 466)}});
  }
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString stillFile = dir.filePath(QStringLiteral("still.png"));
  REQUIRE(still.save(stillFile));
  QQmlComponent imageComponent(&f.engine);
  imageComponent.setData("import QtQuick\nImage { fillMode: Image.PreserveAspectCrop; z: -1 }", QUrl());
  std::unique_ptr<QObject> imageObject(imageComponent.create());
  auto* image = qobject_cast<QQuickItem*>(imageObject.get());
  REQUIRE(image != nullptr);
  image->setProperty("source", QUrl::fromLocalFile(stillFile));
  image->setParentItem(f.window.contentItem());
  image->setSize(QSizeF(Width, Height));
  f.window.resize(Width, Height);
  f.page->setSize(QSizeF(Width, Height));
  f.player->seek(252);
  f.page->setProperty("osdVisible", true);
  QTest::qWait(600);
  f.window.grabWindow().save(shots + QStringLiteral("/readme-player-1600.png"));
}
