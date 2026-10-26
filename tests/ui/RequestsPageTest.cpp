#include "../support/DoctestQt.h"

#include "../support/FakeSeerr.h"

#include <cstdio>
#include <jfd/platform/CredentialStore.h>
#include <jfd/seerr/SeerrService.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/RequestsUi.h>

#include <QAccessible>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_RequestsPlugin)

// The requests screens on a fake Seerr: nothing of them without an address,
// sign-in, browsing, a title's page with its confirmations, the user's own
// requests, and names for every control.

using namespace jfd;
using jfd::test::FakeSeerr;
using jfd::test::waitFor;

namespace {

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root == nullptr) return nullptr;
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  // Every focusable control under `root`.
  void collectFocusable(QQuickItem* root, QList<QQuickItem*>& out) {
    if (root->isVisible() && root->isEnabled() &&
        (root->activeFocusOnTab() ||
         (root->property("focusPolicy").isValid() && root->property("focusPolicy").toInt() != Qt::NoFocus)))
      out.append(root);
    for (QQuickItem* child : root->childItems()) collectFocusable(child, out);
  }

  struct Scene {
    FakeSeerr seerr;
    platform::MemoryCredentialStore store;
    seerr::SeerrService service{store};
    QQmlEngine engine;
    ui::Navigator* navigator = nullptr;
    QQuickWindow window;
    std::unique_ptr<QObject> shellObject;
    QQuickItem* shell = nullptr;

    explicit Scene(bool configured = true) {
      if (configured) service.setAddress(seerr.url().toString());
      ui::RequestsUi::setService(&service);
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      REQUIRE(navigator != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      ui::RequestsUi::registerRoutes(*navigator);
      navigator->reset(QStringLiteral("home"));
      window.resize(1100, 760);
      QQmlComponent component(&engine);
      component.setData("import QtQuick\nimport Jfd.Ui.Shell\nAppShell { width: 1100; height: 760 }",
                        QUrl(QStringLiteral("qrc:/test.qml")));
      shellObject.reset(component.create());
      REQUIRE_MESSAGE(shellObject != nullptr, component.errorString().toStdString());
      shell = qobject_cast<QQuickItem*>(shellObject.get());
      shell->setParentItem(window.contentItem());
      shell->setSize(QSizeF(1100, 760));
      window.show();
    }

    ~Scene() { ui::RequestsUi::setService(nullptr); }

    void signIn(const char* name = "alice", const char* password = "pw-alice") {
      QSignalSpy done(&service, &seerr::SeerrService::signInFinished);
      service.signIn(QString::fromLatin1(name), QString::fromLatin1(password));
      REQUIRE(waitFor([&] { return done.count() == 1; }));
      REQUIRE(done.first().at(0).toInt() == int(seerr::Outcome::Ok));
    }

    // Opens a route and waits for its page.
    QQuickItem* open(const QString& route) {
      REQUIRE(navigator->navigate(route));
      QQuickItem* stack = find(shell, "pageStack");
      REQUIRE(stack != nullptr);
      REQUIRE(waitFor([&] {
        auto* page = stack->property("currentItem").value<QQuickItem*>();
        return page != nullptr && page->property("route").toString() == route;
      }));
      return stack->property("currentItem").value<QQuickItem*>();
    }

    template <class Predicate> bool until(Predicate done) { return waitFor(done); }
  };

  bool visible(QQuickItem* page, const char* name) {
    QQuickItem* item = find(page, name);
    return item != nullptr && item->isVisible();
  }

  QObject* child(QQuickItem* page, const char* name) { return page->findChild<QObject*>(QString::fromLatin1(name)); }

} // namespace

TEST_CASE("Without an address there is no Requests in the sidebar") {
  Scene scene(false);
  CHECK_FALSE(scene.navigator->routeAvailable(QStringLiteral("requests")));
  CHECK(find(scene.shell, "nav_requests") == nullptr);
  CHECK(find(scene.shell, "nav_settings") != nullptr);
  CHECK_FALSE(ui::RequestsUi::service()->configured());

  // The address arrives (Settings): the item appears; cleared, it goes.
  scene.service.setAddress(scene.seerr.url().toString());
  QCoreApplication::processEvents();
  CHECK(scene.navigator->routeAvailable(QStringLiteral("requests")));
  CHECK(scene.until([&] { return find(scene.shell, "nav_requests") != nullptr; }));
  scene.service.setAddress(QString());
  CHECK(scene.until([&] { return find(scene.shell, "nav_requests") == nullptr; }));
}

TEST_CASE("Signed out, the page offers the sign-in and shows no titles") {
  Scene scene;
  QQuickItem* page = scene.open(QStringLiteral("requests"));
  CHECK(visible(page, "requestsSignedOut"));
  CHECK_FALSE(visible(page, "requestsRows"));
  CHECK_FALSE(visible(page, "myRequestsButton"));
  CHECK(find(page, "requestsSignInButton") != nullptr);
}

TEST_CASE("The sign-in page names the server and asks before sending the password") {
  Scene scene;
  std::fprintf(stderr, "[requests-test] step 1\n");
  QQuickItem* page = scene.open(QStringLiteral("seerr-signin"));
  std::fprintf(stderr, "[requests-test] step 2\n");
  QQuickItem* host = find(page, "seerrHost");
  std::fprintf(stderr, "[requests-test] step 3\n");
  REQUIRE(host != nullptr);
  std::fprintf(stderr, "[requests-test] step 4\n");
  CHECK(host->property("text").toString() == scene.service.serverHost());
  std::fprintf(stderr, "[requests-test] step 5\n");
  // Local test server on loopback: no warning about plain http.
  CHECK_FALSE(visible(page, "seerrCleartextWarning"));
  std::fprintf(stderr, "[requests-test] step 6\n");

  auto* user = find(page, "seerrUser");
  std::fprintf(stderr, "[requests-test] step 7\n");
  auto* password = find(page, "seerrPassword");
  std::fprintf(stderr, "[requests-test] step 8\n");
  REQUIRE(user != nullptr);
  std::fprintf(stderr, "[requests-test] step 9\n");
  REQUIRE(password != nullptr);
  std::fprintf(stderr, "[requests-test] step 10\n");
  user->setProperty("text", QStringLiteral("alice"));
  std::fprintf(stderr, "[requests-test] step 11\n");
  password->setProperty("text", QStringLiteral("wrong"));
  std::fprintf(stderr, "[requests-test] step 12\n");
  QMetaObject::invokeMethod(page, "submit");
  std::fprintf(stderr, "[requests-test] step 13\n");
  QObject* confirm = child(page, "seerrConfirm");
  std::fprintf(stderr, "[requests-test] step 14\n");
  REQUIRE(confirm != nullptr);
  std::fprintf(stderr, "[requests-test] step 15\n");
  // Nothing has been sent yet.
  CHECK(scene.seerr.server().requests().isEmpty());
  std::fprintf(stderr, "[requests-test] step 16\n");
  const bool shown = confirm->property("opened").toBool() || confirm->property("visible").toBool();
  std::fprintf(stderr, "[requests-test] step 17\n");
  CHECK(shown);
  std::fprintf(stderr, "[requests-test] step 18\n");
  QMetaObject::invokeMethod(confirm, "accept");
  std::fprintf(stderr, "[requests-test] step 19\n");
  REQUIRE(scene.until([&] {
    QQuickItem* error = find(page, "seerrSignInError");
    return error != nullptr && error->isVisible();
  }));
  std::fprintf(stderr, "[requests-test] step 20\n");
  CHECK_FALSE(scene.service.signedIn());
  std::fprintf(stderr, "[requests-test] step 21\n");
  // The password field is emptied after the answer.
  CHECK(password->property("text").toString().isEmpty());
  std::fprintf(stderr, "[requests-test] step 22\n");

  password->setProperty("text", QStringLiteral("pw-alice"));
  std::fprintf(stderr, "[requests-test] step 23\n");
  QMetaObject::invokeMethod(page, "submit");
  std::fprintf(stderr, "[requests-test] step 24\n");
  QMetaObject::invokeMethod(child(page, "seerrConfirm"), "accept");
  std::fprintf(stderr, "[requests-test] step 25\n");
  REQUIRE(scene.until([&] { return scene.service.signedIn(); }));
  std::fprintf(stderr, "[requests-test] step 26\n");
}

TEST_CASE("Browsing shows the rows, search shows results") {
  Scene scene;
  scene.signIn();
  QQuickItem* page = scene.open(QStringLiteral("requests"));
  CHECK_FALSE(visible(page, "requestsSignedOut"));
  CHECK(visible(page, "requestsRows"));
  CHECK(visible(page, "myRequestsButton"));
  QQuickItem* rows = find(page, "requestsRows");
  // Trending, popular films, popular series each show up with their titles.
  REQUIRE(scene.until([&] {
    int cards = 0;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
      if (item->objectName().isEmpty() && item->property("imageSource").isValid() &&
          item->property("title").isValid() && !item->property("title").toString().isEmpty())
        ++cards;
      for (QQuickItem* c : item->childItems()) walk(c);
    };
    walk(rows);
    return cards >= 4;
  }));

  auto* search = find(page, "requestsSearch");
  REQUIRE(search != nullptr);
  search->setProperty("text", QStringLiteral("matrix"));
  REQUIRE(scene.until([&] { return visible(page, "requestsResults"); }));
  CHECK_FALSE(visible(page, "requestsRows"));
}

TEST_CASE("A movie that is available cannot be requested; the status is in words") {
  Scene scene;
  scene.signIn();
  QQuickItem* page = scene.open(QStringLiteral("seerr-title/movie:603"));
  REQUIRE(scene.until([&] { return visible(page, "titleName"); }));
  CHECK(find(page, "titleName")->property("text").toString() == QStringLiteral("The Matrix"));
  QQuickItem* status = find(page, "titleStatus");
  REQUIRE(status != nullptr);
  CHECK(status->isVisible());
  CHECK(status->property("text").toString() == QStringLiteral("Available"));
  CHECK_FALSE(visible(page, "requestButton"));
}

TEST_CASE("Requesting seasons asks first, all seasons asks separately") {
  Scene scene;
  scene.signIn();
  QQuickItem* page = scene.open(QStringLiteral("seerr-title/tv:1399"));
  REQUIRE(scene.until([&] { return visible(page, "titleName"); }));
  // Nothing chosen: only the all-seasons button.
  CHECK_FALSE(visible(page, "requestButton"));
  REQUIRE(visible(page, "requestAllButton"));
  CHECK(find(page, "season1") != nullptr);
  CHECK(find(page, "season2") != nullptr);
  // Season 1 is available; it cannot be chosen.
  CHECK_FALSE(find(page, "season1")->isEnabled());
  CHECK(find(page, "season2")->isEnabled());

  QMetaObject::invokeMethod(find(page, "season2"), "toggle");
  REQUIRE(scene.until([&] { return visible(page, "requestButton"); }));
  QMetaObject::invokeMethod(find(page, "requestButton"), "clicked");
  QObject* confirm = child(page, "confirmRequest");
  REQUIRE(confirm != nullptr);
  CHECK(scene.seerr.lastRequestBody().isEmpty());
  QMetaObject::invokeMethod(confirm, "accept");
  REQUIRE(scene.until([&] { return !scene.seerr.lastRequestBody().isEmpty(); }));
  CHECK(scene.seerr.lastRequestBody().value(QStringLiteral("seasons")).toArray().first().toInt() == 2);
  REQUIRE(scene.until([&] { return visible(page, "requestResult"); }));
}

TEST_CASE("Asking for all seasons sends 'all' after its own confirmation") {
  Scene scene;
  scene.signIn();
  QQuickItem* page = scene.open(QStringLiteral("seerr-title/tv:1399"));
  REQUIRE(scene.until([&] { return visible(page, "requestAllButton"); }));
  QMetaObject::invokeMethod(find(page, "requestAllButton"), "clicked");
  CHECK(scene.seerr.lastRequestBody().isEmpty());
  QMetaObject::invokeMethod(child(page, "confirmAll"), "accept");
  REQUIRE(scene.until([&] { return !scene.seerr.lastRequestBody().isEmpty(); }));
  CHECK(scene.seerr.lastRequestBody().value(QStringLiteral("seasons")).toString() == QStringLiteral("all"));
}

TEST_CASE("An account that may not request sees no Request button") {
  Scene scene;
  scene.signIn("viewer", "pw-viewer");
  QQuickItem* page = scene.open(QStringLiteral("seerr-title/tv:1399"));
  REQUIRE(scene.until([&] { return visible(page, "titleName"); }));
  CHECK_FALSE(visible(page, "requestAllButton"));
  CHECK_FALSE(visible(page, "requestButton"));
  CHECK(visible(page, "requestsNotAllowed"));
}

TEST_CASE("My requests lists a request and cancels it after a confirmation") {
  Scene scene;
  scene.signIn();
  // A request through the model's own path.
  {
    QQuickItem* title = scene.open(QStringLiteral("seerr-title/tv:1399"));
    REQUIRE(scene.until([&] { return visible(title, "requestAllButton"); }));
    QMetaObject::invokeMethod(find(title, "requestAllButton"), "clicked");
    QMetaObject::invokeMethod(child(title, "confirmAll"), "accept");
    REQUIRE(scene.until([&] { return !scene.seerr.lastRequestBody().isEmpty(); }));
  }
  QQuickItem* page = scene.open(QStringLiteral("requests-mine"));
  QQuickItem* list = find(page, "myRequestsList");
  REQUIRE(list != nullptr);
  REQUIRE(scene.until([&] { return list->property("count").toInt() == 1; }));
  REQUIRE(scene.until([&] { return find(page, "cancelRequestButton") != nullptr; }));
  QMetaObject::invokeMethod(find(page, "cancelRequestButton"), "clicked");
  QObject* confirm = child(page, "cancelRequestConfirm");
  REQUIRE(confirm != nullptr);
  QMetaObject::invokeMethod(confirm, "accept");
  REQUIRE(scene.until([&] { return list->property("count").toInt() == 0; }));
}

TEST_CASE("Every control on the requests pages has a name for screen readers") {
  Scene scene;
  scene.signIn();
  QAccessible::setActive(true);
  for (const char* route : {"requests", "requests-mine", "seerr-signin", "seerr-title/tv:1399"}) {
    QQuickItem* page = scene.open(QString::fromLatin1(route));
    scene.until([&] { return false; }); // let the page settle
    QList<QQuickItem*> focusable;
    collectFocusable(page, focusable);
    CHECK_MESSAGE(!focusable.isEmpty(), route);
    for (QQuickItem* item : focusable) {
      QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item);
      if (iface == nullptr) continue;
      const std::string what = std::string(route) + ": " + item->metaObject()->className() + " " +
                               item->objectName().toStdString() + " has no accessible name";
      CAPTURE(what);
      CHECK(!iface->text(QAccessible::Name).isEmpty());
    }
  }
}
