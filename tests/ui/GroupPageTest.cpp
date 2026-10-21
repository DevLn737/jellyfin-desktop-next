#define DOCTEST_CONFIG_IMPLEMENT
#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/api/SyncPlayApi.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/GroupSession.h>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>
#include <optional>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_GroupPlugin)

// The group page on a live server (scripts/test-server), stock 12.2 or the
// SyncPlay fork (JFD_TEST_SERVER_FORK=1): create a group from the page,
// change its speed, leave, and join another member's group from the list.

using namespace jfd;
using namespace jfd::api;
using jfd::test::waitFor;

namespace {
  QString env(const char* name) { return qEnvironmentVariable(name); }
  bool forkServer() { return env("JFD_TEST_SERVER_FORK") == QLatin1String("1"); }

  template <class T> Result<T> run(std::function<void(Callback<T>)> call, int timeoutMs = 20000) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    if (!waitFor([&] { return out.has_value(); }, timeoutMs)) {
      ApiError error;
      error.kind = ApiError::Kind::Timeout;
      return error;
    }
    return std::move(*out);
  }

  // A signed-in client with its session socket ready.
  struct Connection {
    std::unique_ptr<JellyfinClient> client;
    std::unique_ptr<SessionSocket> socket;
    std::unique_ptr<SyncPlayApi> syncPlay;

    explicit Connection(const QString& deviceId) {
      client = std::make_unique<JellyfinClient>(
          ClientIdentity{QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"),
                         QStringLiteral("CI ") + deviceId, deviceId});
      const auto url = JellyfinClient::normalizeServerUrl(env("JFD_TEST_SERVER_URL"));
      REQUIRE_MESSAGE(url.has_value(), "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
      client->setServerUrl(*url);
      REQUIRE(run<AuthenticationResult>([&](auto done) {
        client->signIn(env("JFD_TEST_USER"), env("JFD_TEST_PASSWORD"), nullptr, done);
      }).ok());
      SessionCapabilities capabilities;
      capabilities.supportsSyncPlayPlaybackRate = true;
      capabilities.deviceProfile = buildDeviceProfile({});
      REQUIRE(run<Empty>([&](auto done) { client->reportCapabilities(capabilities, nullptr, done); }).ok());
      socket = std::make_unique<SessionSocket>(*client);
      syncPlay = std::make_unique<SyncPlayApi>(*client);
      QSignalSpy ready(socket.get(), &SessionSocket::ready);
      socket->start();
      REQUIRE(waitFor([&] { return ready.count() == 1; }, 20000));
    }
  };

  // Walks the visual tree: Repeater delegates are not QObject children.
  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  // Buttons are disabled while the model waits on the server.
  void click(QQuickItem* item) {
    REQUIRE(item != nullptr);
    REQUIRE(item->isVisible());
    REQUIRE(waitFor([item] { return item->isEnabled(); }, 20000));
    QMetaObject::invokeMethod(item, "clicked");
  }

  bool shown(QQuickItem* root, const char* name) {
    QQuickItem* item = find(root, name);
    return item != nullptr && item->isVisible();
  }
} // namespace

TEST_CASE("The group page creates a group, changes its speed, leaves and joins another") {
  Connection host(QStringLiteral("jfd-group-page-host"));
  Connection guest(QStringLiteral("jfd-group-page-guest"));
  session::SessionRuntime::Settings settings;
  settings.deviceProfile = buildDeviceProfile({});
  session::SessionRuntime runtime(
      *host.client, *host.socket,
      [](const core::Clock& clock, session::SessionRuntime::Post post) {
        return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
      },
      settings);
  session::GroupModel model(runtime, *host.client, *host.socket);
  ui::GroupSession::setInstance(&model);
  if (forkServer()) REQUIRE(waitFor([&] { return model.rateSupported(); }, 10000));

  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Group/GroupPage.qml")));
  std::unique_ptr<QObject> object(component.create());
  REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
  QQuickWindow window;
  window.resize(1000, 900);
  auto* page = qobject_cast<QQuickItem*>(object.get());
  REQUIRE(page != nullptr);
  page->setParentItem(window.contentItem());
  page->setSize(QSizeF(1000, 900));
  window.show();

  // Outside a group: the name field and the server's list.
  CHECK(shown(page, "createButton"));
  CHECK_FALSE(shown(page, "leaveButton"));
  QQuickItem* nameField = find(page, "groupNameField");
  REQUIRE(nameField != nullptr);
  nameField->setProperty("text", QStringLiteral("jfd-group-page"));
  click(find(page, "createButton"));
  REQUIRE(waitFor([&] { return model.inGroup() && shown(page, "leaveButton"); }, 20000));
  CHECK(model.groupName() == QStringLiteral("jfd-group-page"));
  CHECK_FALSE(shown(page, "createButton"));

  // Speed: offered and applied on the fork, hidden on stock 12.2.
  QQuickItem* rateBox = find(page, "rateBox");
  REQUIRE(rateBox != nullptr);
  if (forkServer()) {
    REQUIRE(waitFor([&] { return rateBox->isVisible(); }, 5000));
    const QVariantList rates = page->property("rates").toList();
    const int index = int(rates.indexOf(QVariant(1.5)));
    REQUIRE(index >= 0);
    rateBox->setProperty("currentIndex", index);
    QMetaObject::invokeMethod(rateBox, "activated", Q_ARG(int, index));
    REQUIRE(waitFor([&] { return model.rate() == doctest::Approx(1.5); }, 20000));
  } else {
    CHECK_FALSE(rateBox->isVisible());
  }

  click(find(page, "leaveButton"));
  REQUIRE(waitFor([&] { return !model.inGroup() && shown(page, "createButton"); }, 20000));

  // Another member's group shows in the list; clicking it joins.
  auto made = run<SyncPlayGroup>([&](auto done) {
    guest.syncPlay->createGroup(QStringLiteral("jfd-group-page-guest"), nullptr, done);
  });
  REQUIRE(made.ok());
  click(find(page, "refreshButton"));
  const QByteArray itemName = "group_" + made->groupId.toLatin1();
  REQUIRE(waitFor([&] { return shown(page, itemName.constData()); }, 20000));
  click(find(page, itemName.constData()));
  REQUIRE(waitFor([&] { return model.inGroup() && model.groupId() == made->groupId; }, 20000));
  // The server lists each user once on joining, and both sessions here
  // sign in as the same test user.
  CHECK(model.participants().contains(env("JFD_TEST_USER")));
  CHECK(shown(page, "groupState"));

  click(find(page, "leaveButton"));
  REQUIRE(waitFor([&] { return !model.inGroup(); }, 20000));
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->leaveGroup(nullptr, done); }).ok());
  object.reset();
  ui::GroupSession::setInstance(nullptr);
}

// Qt Quick needs a GUI application; headless runs use the offscreen platform.
int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  doctest::Context context(argc, argv);
  return context.run();
}
