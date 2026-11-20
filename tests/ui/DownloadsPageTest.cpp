#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../offline/OfflineFixtures.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/HomeModel.h>
#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/DownloadsController.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/ui/DownloadsUi.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

// The download screens in an offscreen window, driven with the keyboard
// only (a gamepad reaches them as the same keys through InputRouter): the
// Downloads page (queue actions, tabs, filter, delete with its question),
// the Download button and its dialog with the total, the button gone
// without the server's permission, the navigation entry, and the home page
// without the server showing the downloads, which play.

using namespace jfd;
using namespace jfd::offline;
using jfd::test::waitFor;

namespace {

  api::ClientIdentity identity() {
    return api::ClientIdentity{QStringLiteral("Test Client"), QStringLiteral("1.0"),
                               QStringLiteral("Test Device"), QStringLiteral("device1")};
  }

  const QString SeasonId = QStringLiteral("5ea5000000000000000000000000000a");

  QJsonObject video(int n, const QString& type, const QString& name, qint64 size, bool played = false) {
    const QString id = test::itemIdFor(n);
    QJsonObject item = test::itemJson(id, test::mediaSourceJson(id, size), type);
    item.insert(QStringLiteral("Name"), name);
    item.insert(QStringLiteral("UserData"), QJsonObject{{QStringLiteral("Played"), played}});
    if (type == QLatin1String("Episode")) {
      item.insert(QStringLiteral("SeriesName"), QStringLiteral("Bob and the Tests"));
      item.insert(QStringLiteral("SeriesId"), QStringLiteral("5e1e5000000000000000000000000001"));
      item.insert(QStringLiteral("SeasonId"), SeasonId);
      item.insert(QStringLiteral("ParentIndexNumber"), 1);
      item.insert(QStringLiteral("IndexNumber"), n);
    }
    return item;
  }

  QJsonObject song(int n, const QString& name, qint64 size) {
    QJsonObject item = video(n, QStringLiteral("Audio"), name, size);
    item.insert(QStringLiteral("AlbumId"), QStringLiteral("a1b0a000000000000000000000000001"));
    item.insert(QStringLiteral("Album"), QStringLiteral("Alice Sings"));
    item.remove(QStringLiteral("VideoType"));
    return item;
  }

  struct Scene {
    bool mayDownload = true;
    QHash<QString, QJsonObject> items;
    QHash<QString, QJsonArray> children;
    test::FakeServer server{[this](const test::HttpRequest& r) { return handle(r); }};
    api::JellyfinClient client{identity()};
    library::HomeModel home{client};

    QTemporaryDir dir;
    DownloadStore store{dir.filePath(QStringLiteral("downloads"))};
    OfflineCatalog catalog{store};
    std::shared_ptr<test::FakeStorageGuard> guard = std::make_shared<test::FakeStorageGuard>(0, 0);
    std::unique_ptr<DownloadQueue> queue;
    std::unique_ptr<DownloadsController> controller;

    QQmlEngine engine;
    ui::Navigator* navigator = nullptr;
    ui::LibraryUi* library = nullptr;
    ui::DownloadsUi* downloads = nullptr;
    QQuickWindow view;
    QQuickWindow* window = &view;
    std::unique_ptr<QObject> root;
    QStringList played;

    Scene() {
      client.setServerUrl(server.url());
      client.setSession(QString::fromLatin1(test::Token), test::UserId);
      catalog.load();
      queue = std::make_unique<DownloadQueue>(store, catalog, guard);
      // No server address for the files: queued items wait, nothing transfers.
      queue->setAccess(test::ServerId, test::UserId, QUrl(), {});
      controller = std::make_unique<DownloadsController>(store, catalog, *queue, *guard);
      controller->setUser(test::ServerId, test::UserId);
      guard->free = 40 * GiB;
      guard->setLimitBytes(100 * GiB);
      ui::DownloadsUi::setService(controller.get());

      // Loads the modules, so their types have ids (singletonInstance by id
      // works on every Qt 6 this builds with).
      {
        QQmlComponent imports(&engine);
        imports.setData("import QtQml\nimport Jfd.Ui.Shell\nimport Jfd.Ui.Library\nQtObject {}",
                        QUrl(QStringLiteral("qrc:/imports.qml")));
        delete imports.create();
      }
      navigator = engine.singletonInstance<ui::Navigator*>(qmlTypeId("Jfd.Ui.Shell", 1, 0, "Navigator"));
      REQUIRE(navigator != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      navigator->registerPage(QStringLiteral("home"), QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Library/HomePage.qml")));
      ui::DownloadsUi::registerRoutes(*navigator);
      library = engine.singletonInstance<ui::LibraryUi*>(qmlTypeId("Jfd.Ui.Library", 1, 0, "Library"));
      downloads = engine.singletonInstance<ui::DownloadsUi*>(qmlTypeId("Jfd.Ui.Library", 1, 0, "Downloads"));
      REQUIRE(downloads != nullptr);
      REQUIRE(library != nullptr);
      ui::LibraryUi::Services services;
      services.client = &client;
      services.home = &home;
      library->attach(services);
      library->setPlayHandler([this](const QString& id, qint64) { played.append(id); });
    }

    ~Scene() {
      root.reset();
      library->attach({});
      ui::DownloadsUi::setService(nullptr);
    }

    void allow(bool may) {
      mayDownload = may;
      controller->setClient(&client);
      controller->setOnline(true);
      REQUIRE(waitFor([&] { return controller->permissionKnown(); }));
    }

    test::HttpResponse handle(const test::HttpRequest& r) {
      const auto json = [](const QJsonObject& o) { return test::HttpResponse{200, QJsonDocument(o).toJson()}; };
      if (r.path == QLatin1String("/Users/Me"))
        return json({{QStringLiteral("Id"), test::UserId},
                     {QStringLiteral("Name"), QStringLiteral("alice")},
                     {QStringLiteral("Policy"),
                      QJsonObject{{QStringLiteral("EnableContentDownloading"), mayDownload}}}});
      if (r.path == QLatin1String("/Items") && r.query.hasQueryItem(QStringLiteral("Recursive")) &&
          r.query.hasQueryItem(QStringLiteral("ParentId")))
        return json({{QStringLiteral("Items"), children.value(r.query.queryItemValue(QStringLiteral("ParentId")))}});
      if (r.path == QLatin1String("/Items") && r.query.hasQueryItem(QStringLiteral("ids"))) {
        QJsonArray found;
        for (const QString& id : r.query.queryItemValue(QStringLiteral("ids")).split(QLatin1Char(',')))
          if (items.contains(id)) found.append(items[id]);
        return json({{QStringLiteral("Items"), found}});
      }
      // The rest of the server is down.
      return {503, "{}"};
    }

    void addComplete(const QJsonObject& item, qint64 bytes) {
      ItemManifest m;
      m.serverId = test::ServerId;
      m.userId = test::UserId;
      m.itemId = item.value(QStringLiteral("Id")).toString();
      m.state = ItemState::Complete;
      m.item = item;
      m.mediaSource = item.value(QStringLiteral("MediaSources")).toArray().first().toObject();
      m.userData = item.value(QStringLiteral("UserData")).toObject();
      m.mediaFile = QStringLiteral("media.mkv");
      m.expectedSize = bytes;
      m.queuedAt = m.completedAt = QDateTime::currentDateTimeUtc();
      REQUIRE(store.writeManifest(m));
      QFile file(store.mediaPath(m));
      REQUIRE(file.open(QIODevice::WriteOnly));
      file.write(QByteArray(int(bytes), 'x'));
      catalog.update(m);
    }

    void addQueued(const QJsonObject& item) {
      const QJsonObject source = item.value(QStringLiteral("MediaSources")).toArray().first().toObject();
      REQUIRE(queue->enqueueOriginal(item, source) == EnqueueResult::Queued);
    }

    // `qml` is an item; it fills the window.
    void load(const char* qml) {
      QQmlComponent component(&engine);
      component.setData(qml, QUrl(QStringLiteral("qrc:/downloads-test.qml")));
      root.reset(component.create());
      REQUIRE_MESSAGE(root != nullptr, component.errorString().toStdString());
      auto* item = qobject_cast<QQuickItem*>(root.get());
      REQUIRE(item != nullptr);
      view.resize(1280, 800);
      item->setParentItem(view.contentItem());
      // findChild on the window walks QObject children, not visual ones.
      item->setParent(view.contentItem());
      item->setSize(QSizeF(1280, 800));
      view.show();
      view.requestActivate();
    }

    void loadShell() { load("import QtQuick\nimport Jfd.Ui.Shell\nAppShell { objectName: \"shell\" }"); }

    QQuickItem* page() const {
      auto* stack = window->findChild<QQuickItem*>(QStringLiteral("pageStack"));
      return stack == nullptr ? nullptr : stack->property("currentItem").value<QQuickItem*>();
    }

    QQuickItem* open(const QString& route) {
      REQUIRE(navigator->navigate(route));
      REQUIRE(waitFor([&] { return page() != nullptr && page()->property("route").toString() == route; }));
      return page();
    }

    QQuickItem* focused() const { return window->activeFocusItem(); }

    void key(Qt::Key k) {
      QTest::keyClick(window, k);
      QCoreApplication::processEvents();
    }

    // The focused item, or one of its parents, has this objectName.
    bool focusIn(const QString& name) const {
      for (QQuickItem* item = focused(); item != nullptr; item = item->parentItem())
        if (item->objectName() == name) return true;
      return false;
    }
  };

  QQuickItem* find(QQuickItem* root, const QString& name) {
    if (root == nullptr) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  bool shown(QQuickItem* root, const QString& name) {
    QQuickItem* item = find(root, name);
    return item != nullptr && item->isVisible();
  }

} // namespace

TEST_CASE("The Downloads page is in the account menu and works with the keyboard alone") {
  Scene scene;
  const QJsonObject q1 = video(1, QStringLiteral("Movie"), QStringLiteral("Alice Waits"), 3000);
  const QJsonObject q2 = video(2, QStringLiteral("Movie"), QStringLiteral("Bob Waits"), 4000);
  scene.addQueued(q1);
  scene.addQueued(q2);
  scene.addComplete(song(3, QStringLiteral("Song for Alice"), 1000), 1000);
  scene.addComplete(video(4, QStringLiteral("Episode"), QStringLiteral("Pilot"), 2000, true), 2000);
  scene.controller->refresh();
  const QString id1 = q1.value(QStringLiteral("Id")).toString();
  const QString id2 = q2.value(QStringLiteral("Id")).toString();
  const QString songId = test::itemIdFor(3);

  scene.loadShell();
  scene.navigator->reset(QStringLiteral("home"));
  // In the account menu of the navigation bar (online).
  REQUIRE(waitFor([&] {
    QQuickItem* nav = find(scene.window->contentItem(), QStringLiteral("topNav"));
    if (nav == nullptr) return false;
    for (const QVariant& entry : nav->property("accountEntries").toList())
      if (entry.toMap().value(QStringLiteral("route")).toString() == QStringLiteral("downloads")) return true;
    return false;
  }));

  QQuickItem* page = scene.open(QStringLiteral("downloads"));
  CHECK(shown(page, QStringLiteral("storageBar")));
  QQuickItem* storage = find(page, QStringLiteral("storageText"));
  REQUIRE(storage != nullptr);
  // 3000 + 4000 bytes not yet on disk; 3000 bytes complete.
  CHECK(storage->property("text").toString().contains(QStringLiteral("of 100.0 GiB")));
  CHECK(storage->property("text").toString().contains(QStringLiteral("40.0 GiB free")));

  // The queue first, its tab focused.
  REQUIRE(waitFor([&] { return scene.focusIn(QStringLiteral("queueTab")); }));
  QQuickItem* list = find(page, QStringLiteral("queueList"));
  REQUIRE(list != nullptr);
  CHECK(list->property("count").toInt() == 2);

  // Down: "Pause all"; Down: the first row.
  scene.key(Qt::Key_Down);
  CHECK(scene.focusIn(QStringLiteral("pauseAllButton")));
  scene.key(Qt::Key_Down);
  REQUIRE(scene.focusIn(QStringLiteral("downloadRow_") + id1));
  // Enter pauses the row's download; Enter again continues it.
  scene.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return scene.controller->stateOf(id1) == QStringLiteral("paused"); }));
  REQUIRE(waitFor([&] {
    QQuickItem* status = find(find(page, QStringLiteral("downloadRow_") + id1), QStringLiteral("downloadStatus"));
    return status != nullptr && status->property("text").toString() == QStringLiteral("Paused");
  }));
  scene.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return scene.controller->stateOf(id1) == QStringLiteral("queued"); }));
  // Right walks the row's buttons: pause, (move up is disabled on the
  // first row) move down.
  scene.key(Qt::Key_Right);
  CHECK(scene.focusIn(QStringLiteral("pauseButton")));
  scene.key(Qt::Key_Right);
  CHECK(scene.focusIn(QStringLiteral("moveDownButton")));
  scene.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return scene.queue->order() == QStringList{id2, id1}; }));
  REQUIRE(waitFor([&] { return scene.controller->queueModel()->itemIdAt(0) == id2; }));

  // Up to the tabs, Right to "Downloaded", Enter.
  // The row is last now, so "move down" is disabled and the row itself
  // has the focus back. Up walks the rows, the toolbar, then the tabs.
  REQUIRE(waitFor([&] { return scene.focusIn(QStringLiteral("downloadRow_") + id1); }));
  for (int i = 0; i < 4 && !scene.focusIn(QStringLiteral("queueTab")); ++i) scene.key(Qt::Key_Up);
  REQUIRE(scene.focusIn(QStringLiteral("queueTab")));
  scene.key(Qt::Key_Right);
  REQUIRE(scene.focusIn(QStringLiteral("downloadedTab")));
  scene.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return find(page, QStringLiteral("downloadedList")) != nullptr; }));
  list = find(page, QStringLiteral("downloadedList"));
  CHECK(list->property("count").toInt() == 2);
  CHECK(shown(page, QStringLiteral("deleteWatchedButton")));

  // Down: the filter. Typing filters the list.
  scene.key(Qt::Key_Down);
  REQUIRE(scene.focusIn(QStringLiteral("downloadsFilter")));
  for (const char letter : {'s', 'o', 'n', 'g'}) QTest::keyClick(scene.window, letter);
  REQUIRE(waitFor([&] { return list->property("count").toInt() == 1; }));
  // Down: the row; Enter plays the track (no music player here: the
  // library's handler is asked), Right twice: Delete, Enter: the question.
  scene.key(Qt::Key_Down);
  REQUIRE(scene.focusIn(QStringLiteral("downloadRow_") + songId));
  scene.key(Qt::Key_Right);
  CHECK(scene.focusIn(QStringLiteral("playDownloadButton")));
  scene.key(Qt::Key_Right);
  REQUIRE(scene.focusIn(QStringLiteral("deleteButton")));
  scene.key(Qt::Key_Return);
  QObject* confirm = page->findChild<QObject*>(QStringLiteral("confirmDownloadsDelete"));
  REQUIRE(confirm != nullptr);
  REQUIRE(waitFor([&] { return confirm->property("opened").toBool(); }));
  CHECK(confirm->property("text").toString().contains(QStringLiteral("Song for Alice")));
  REQUIRE(waitFor([&] { return scene.focusIn(QStringLiteral("dialogAccept")); }));
  scene.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return scene.controller->stateOf(songId).isEmpty(); }));
  CHECK_FALSE(scene.catalog.find(test::ServerId, test::UserId, songId));
}

TEST_CASE("Download shows the total and queues on Enter; without permission there is no button") {
  Scene scene;
  scene.allow(true);
  const QJsonObject e1 = video(11, QStringLiteral("Episode"), QStringLiteral("One"), 300 * MiB);
  const QJsonObject e2 = video(12, QStringLiteral("Episode"), QStringLiteral("Two"), 200 * MiB);
  scene.items.insert(SeasonId, QJsonObject{{QStringLiteral("Id"), SeasonId},
                                           {QStringLiteral("Name"), QStringLiteral("Season 1")},
                                           {QStringLiteral("Type"), QStringLiteral("Season")},
                                           {QStringLiteral("IsFolder"), true}});
  scene.children.insert(SeasonId, QJsonArray{e1, e2});
  QSignalSpy messages(scene.downloads, &ui::DownloadsUi::message);
  scene.load(R"(
    import QtQuick
    import Jfd.Ui.Library
    Item {
        DownloadActions {
            objectName: "actions"
            itemId: "5ea5000000000000000000000000000a"
            itemType: "Season"
            itemName: "Season 1"
        }
    })");
  QQuickItem* root = scene.window->contentItem();
  QQuickItem* button = find(root, QStringLiteral("downloadButton"));
  REQUIRE(button != nullptr);
  REQUIRE(waitFor([&] { return button->isVisible(); }));
  CHECK_FALSE(shown(root, QStringLiteral("downloadNotAllowed")));
  CHECK_FALSE(shown(root, QStringLiteral("deleteDownloadButton")));
  button->forceActiveFocus(Qt::TabFocusReason);
  scene.key(Qt::Key_Return);

  QObject* dialog = scene.window->findChild<QObject*>(QStringLiteral("downloadDialog"));
  REQUIRE(dialog != nullptr);
  REQUIRE(waitFor([&] { return dialog->property("opened").toBool(); }));
  QObject* summary = scene.window->findChild<QObject*>(QStringLiteral("downloadSummary"));
  REQUIRE(summary != nullptr);
  REQUIRE(waitFor([&] { return summary->property("text").toString().contains(QStringLiteral("500 MiB")); }));
  CHECK(summary->property("text").toString().startsWith(QStringLiteral("2 item")));
  // A season offers "only unwatched".
  QObject* unwatched = scene.window->findChild<QObject*>(QStringLiteral("downloadUnwatched"));
  REQUIRE(unwatched != nullptr);
  CHECK(unwatched->property("visible").toBool());
  CHECK(scene.queue->order().isEmpty());
  // The accept button has the focus: Enter queues both.
  REQUIRE(waitFor([&] { return scene.focusIn(QStringLiteral("dialogAccept")); }));
  scene.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return scene.queue->order().size() == 2; }));
  REQUIRE(waitFor([&] { return button->property("text").toString().startsWith(QStringLiteral("Downloading")); }));
  CHECK(shown(root, QStringLiteral("deleteDownloadButton")));
  CHECK_FALSE(messages.isEmpty());

  // The server takes the permission away (a new sign-in asks again): no
  // Download, a line says why; Delete stays for what is there.
  scene.mayDownload = false;
  scene.controller->setUser(QString(), QString());
  scene.controller->setUser(test::ServerId, test::UserId);
  REQUIRE(waitFor([&] { return scene.controller->permissionKnown(); }));
  REQUIRE(waitFor([&] { return shown(root, QStringLiteral("downloadNotAllowed")); }));
  CHECK_FALSE(button->isVisible());
  CHECK(shown(root, QStringLiteral("deleteDownloadButton")));
}

TEST_CASE("Without the server the home page shows the downloads, which play from the keyboard") {
  Scene scene;
  scene.addComplete(video(21, QStringLiteral("Movie"), QStringLiteral("Alice Offline"), 1000), 1000);
  scene.addComplete(video(22, QStringLiteral("Episode"), QStringLiteral("Pilot"), 1000), 1000);
  scene.addComplete(song(23, QStringLiteral("Song for Bob"), 1000), 1000);
  scene.controller->refresh();
  scene.library->setOnline(false);
  scene.loadShell();
  scene.navigator->reset(QStringLiteral("home"));
  REQUIRE(waitFor([&] { return scene.page() != nullptr && scene.page()->property("route").toString() == QStringLiteral("home"); }));
  QQuickItem* page = scene.page();

  REQUIRE(waitFor([&] { return shown(page, QStringLiteral("offlineRows")); }));
  CHECK_FALSE(shown(page, QStringLiteral("homeRows")));
  REQUIRE(waitFor([&] { return shown(page, QStringLiteral("offlineBanner")); }));
  REQUIRE(waitFor([&] { return find(page, QStringLiteral("offlineRow_movies")) != nullptr; }));
  CHECK(find(page, QStringLiteral("offlineRow_episodes")) != nullptr);
  CHECK(find(page, QStringLiteral("offlineRow_music")) != nullptr);
  CHECK(find(page, QStringLiteral("offlineRow_resume")) == nullptr);
  // The card says it is downloaded.
  QQuickItem* badge = find(find(page, QStringLiteral("offlineRow_movies")), QStringLiteral("downloadBadge"));
  REQUIRE(badge != nullptr);
  CHECK(badge->property("downloadState").toString() == QStringLiteral("complete"));

  // The first row's first card has the focus; Enter plays the film.
  REQUIRE(waitFor([&] { return scene.focused() != nullptr && scene.focused()->property("itemId").isValid(); }));
  scene.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return !scene.played.isEmpty(); }));
  CHECK(scene.played.first() == test::itemIdFor(21));
}
