#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/music/MusicPlayer.h>
#include <jfd/playback/StreamPlan.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/MusicUi.h>
#include <jfd/ui/Navigator.h>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_MusicPlugin)

// The music screens on the session runtime with a virtual backend and a fake
// server: a library of 10,000 albums and 50,000 tracks scrolls by pages, the
// mini-player and pages show what plays, music is not offered in a SyncPlay
// group, and starting a track during a movie asks first.

using namespace jfd;
using jfd::test::waitFor;

namespace {
  constexpr int Albums = 10'000;
  constexpr int Tracks = 50'000;

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  test::HttpResponse items(const test::HttpRequest& request) {
    const QString path = request.path;
    if (path.endsWith(QLatin1String("/UserViews"))) {
      return test::json(R"({"Items": [{"Id": "lib", "Name": "Music", "Type": "CollectionFolder",
                            "CollectionType": "music"}], "TotalRecordCount": 1})");
    }
    if (!path.endsWith(QLatin1String("/Items"))) return test::json(R"({"Items": [], "TotalRecordCount": 0})");
    const QString type = request.query.queryItemValue(QStringLiteral("includeItemTypes"));
    const int start = request.query.queryItemValue(QStringLiteral("startIndex")).toInt();
    const int limit = std::max(1, request.query.queryItemValue(QStringLiteral("limit")).toInt());
    const bool albums = type == QLatin1String("MusicAlbum");
    const int total = albums ? Albums : Tracks;
    QJsonArray out;
    for (int i = start; i < std::min(total, start + limit); ++i) {
      QJsonObject o{{QStringLiteral("Id"), (albums ? QStringLiteral("album") : QStringLiteral("track")) + QString::number(i)},
                    {QStringLiteral("Name"), (albums ? QStringLiteral("Album ") : QStringLiteral("Track ")) + QString::number(i)},
                    {QStringLiteral("Type"), albums ? QStringLiteral("MusicAlbum") : QStringLiteral("Audio")},
                    {QStringLiteral("AlbumArtist"), QStringLiteral("Artist")},
                    {QStringLiteral("RunTimeTicks"), 1800000000LL}};
      out.append(o);
    }
    return test::json(QJsonDocument(QJsonObject{{QStringLiteral("Items"), out},
                                                {QStringLiteral("TotalRecordCount"), total},
                                                {QStringLiteral("StartIndex"), start}})
                          .toJson(QJsonDocument::Compact));
  }

  struct Fixture {
    test::FakeServer server{items};
    api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"),
                                                    QStringLiteral("CI"), QStringLiteral("jfd-music-ui")}};
    api::SessionSocket socket{client};
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<music::MusicPlayer> player;
    std::unique_ptr<ui::MusicUi> ui;
    QQmlEngine engine;

    Fixture() {
      client.setServerUrl(server.url(QStringLiteral("/")));
      client.setSession(QStringLiteral("token"), QStringLiteral("user"));
      session::SessionRuntime::Settings settings;
      settings.deviceProfile = api::buildDeviceProfile({});
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
          },
          settings);
      player = std::make_unique<music::MusicPlayer>(*runtime, client);
      ui::MusicUi::Config config;
      config.client = &client;
      config.player = player.get();
      ui = std::make_unique<ui::MusicUi>(config);
      ui::Music::setInstance(ui.get());
      ui::MusicSession::setInstance(player.get());
      REQUIRE(waitFor([&] { return ui->available(); }));
    }
    ~Fixture() {
      ui::MusicSession::setInstance(nullptr);
      ui::Music::setInstance(nullptr);
    }

    std::unique_ptr<QObject> create(const char* qml) {
      QQmlComponent component(&engine);
      component.setData(qml, QUrl());
      std::unique_ptr<QObject> object(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      return object;
    }
  };
} // namespace

TEST_CASE("A library of 10,000 albums and 50,000 tracks loads its pages as they come into view") {
  Fixture f;
  ui::MusicListModel albums(f.client, *f.player);
  QElapsedTimer timer;
  timer.start();
  albums.setKind(QStringLiteral("albums"));
  REQUIRE(waitFor([&] { return albums.count() == Albums; }));
  // The first page decides the count: the list is usable at once.
  MESSAGE("music: 10,000 albums listed in " << timer.elapsed() << " ms");
  CHECK(timer.elapsed() < 1000);
  CHECK(albums.loadedPages() == 1);
  CHECK(albums.rowCount() == Albums);

  // The last rows load their own page and nothing between.
  timer.restart();
  albums.ensureLoaded(Albums - 1);
  REQUIRE(waitFor([&] { return albums.loadedPages() == 2; }));
  MESSAGE("music: the last page of albums in " << timer.elapsed() << " ms");
  CHECK(timer.elapsed() < 1000);
  CHECK(albums.data(albums.index(Albums - 1), ui::MusicListModel::NameRole).toString() ==
        QStringLiteral("Album %1").arg(Albums - 1));

  ui::MusicListModel songs(f.client, *f.player);
  songs.setKind(QStringLiteral("songs"));
  REQUIRE(waitFor([&] { return songs.count() == Tracks; }));
  CHECK(songs.loadedPages() == 1);
  songs.ensureLoaded(25'000);
  REQUIRE(waitFor([&] { return songs.loadedPages() == 2; }));
}

TEST_CASE("The music page lists the library by tab and the mini-player follows the queue") {
  Fixture f;
  QQuickWindow window;
  window.resize(1280, 720);
  auto page = f.create(R"(
import QtQuick
import Jfd.Ui.Music
MusicPage { width: 1280; height: 720 }
)");
  auto* item = qobject_cast<QQuickItem*>(page.get());
  item->setParentItem(window.contentItem());
  window.show();

  QQuickItem* grid = find(item, "musicGrid");
  REQUIRE(grid != nullptr);
  CHECK(waitFor([&] { return find(item, "musicCount") != nullptr; }));
  QQuickItem* count = find(item, "musicCount");
  CHECK(waitFor([&] { return qvariant_cast<QString>(count->property("text")).contains(QString::number(Albums)); }));

  // Songs are a list of rows, not cards.
  QQuickItem* songsTab = find(item, "musicTab_songs");
  REQUIRE(songsTab != nullptr);
  QMetaObject::invokeMethod(songsTab, "clicked");
  QQuickItem* songs = find(item, "musicSongs");
  REQUIRE(songs != nullptr);
  CHECK(waitFor([&] { return songs->isVisible() && !grid->isVisible(); }));
  CHECK(waitFor([&] { return qvariant_cast<QString>(count->property("text")).contains(QString::number(Tracks)); }));

  // Nothing queued: no strip. With a track queued it shows its title.
  auto mini = f.create(R"(
import QtQuick
import Jfd.Ui.Music
MiniPlayer { }
)");
  auto* bar = qobject_cast<QQuickItem*>(mini.get());
  bar->setParentItem(window.contentItem());
  CHECK_FALSE(bar->isVisible());
  music::Track track;
  track.id = QStringLiteral("t1");
  track.name = QStringLiteral("Queued Song");
  track.artist = QStringLiteral("Someone");
  f.player->append({track});
  f.player->jumpTo(0);
  const bool shown = waitFor([&] { return bar->isVisible() && bar->height() > 0; });
  CHECK(shown);
}

TEST_CASE("Music is not offered in a SyncPlay group and a track during a movie asks first") {
  Fixture f;
  auto guard = f.create(R"(
import QtQuick
import Jfd.Ui.Music
Item {
    property int started: 0
    property alias guard: g
    PlayGuard { id: g }
    function go() { g.run(() => started++) }
}
)");
  auto* root = qobject_cast<QQuickItem*>(guard.get());
  REQUIRE(root != nullptr);

  // Plain: the action runs at once.
  QMetaObject::invokeMethod(root, "go");
  CHECK(root->property("started").toInt() == 1);

  // A movie plays: the action waits for the answer.
  core::MediaRequest movie;
  movie.url = "https://srv/Videos/movie/stream";
  movie.title = "Movie";
  f.runtime->post(core::intent::Open{movie});
  CHECK(waitFor([&] { return f.player->videoActive(); }));
  QMetaObject::invokeMethod(root, "go");
  CHECK(root->property("started").toInt() == 1);
  CHECK(root->property("guard").value<QObject*>()->property("pending").isValid());

  // In a group: no music, nothing starts.
  core::SessionSnapshot snapshot;
  core::GroupSnapshot group;
  snapshot.group = group;
  f.runtime->hub().snapshotChanged(snapshot);
  CHECK(waitFor([&] { return !f.player->available(); }));
  QMetaObject::invokeMethod(root, "go");
  CHECK(root->property("started").toInt() == 1);
}
