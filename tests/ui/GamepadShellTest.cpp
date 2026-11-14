// The gamepad on the real shell: Back (B on Xbox) leaves the page through the
// shell's Escape handling, Search (Y) opens the Ctrl+K palette and Back
// closes it again; in TV mode the d-pad, the shoulder buttons and View move
// through the shell, and the button check takes the presses. SDL's virtual
// joystick → GamepadInput → (GamepadStatus) → InputRouter.
#include "../support/DoctestQt.h"
#include "../platform/VirtualGamepad.h"

#include <jfd/platform/GamepadInput.h>
#include <jfd/ui/GamepadStatus.h>
#include <jfd/ui/InputRouter.h>
#include <jfd/ui/Navigator.h>

#include <QElapsedTimer>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

using namespace jfd;
using jfd::test::SdlGamepadSubsystem;
using jfd::test::VirtualGamepad;

namespace {

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  template <class Predicate> bool pumpUntil(Predicate done, int timeoutMs = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
      if (timer.elapsed() > timeoutMs) return false;
      QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
    return true;
  }

  void pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  struct Scene {
    Scene() {
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      navigator->reset(QStringLiteral("home"));
      window.resize(1000, 700);
      QQmlComponent component(&engine);
      component.setData(R"(
import QtQuick
import Jfd.Ui.Shell
import Jfd.Ui.Library
Item {
  width: 1000; height: 700
  AppShell { id: shell; anchors.fill: parent; onPaletteRequested: palette.toggle() }
  CommandPalette { id: palette; objectName: "palette"; anchors.fill: parent }
}
)",
                        QUrl(QStringLiteral("qrc:/gamepad-shell.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      root = qobject_cast<QQuickItem*>(object.get());
      root->setParentItem(window.contentItem());
      window.show();
      pump(50);
      palette = find(root, "palette");
      REQUIRE(palette != nullptr);

      router.setWindow(&window);
      QObject::connect(&input, &platform::GamepadInput::actionTriggered, &router, &ui::InputRouter::route);
      REQUIRE(sdl.ok);
      REQUIRE(input.start());
    }

    void tap(VirtualGamepad& pad, int button) {
      int actions = 0;
      auto connection = QObject::connect(&input, &platform::GamepadInput::actionTriggered, [&] { ++actions; });
      pad.button(button, true);
      REQUIRE(pumpUntil([&] { return actions > 0; }));
      pad.button(button, false);
      pump(30);
      QObject::disconnect(connection);
    }

    SdlGamepadSubsystem sdl;
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> object;
    QQuickItem* root = nullptr;
    QQuickItem* palette = nullptr;
    ui::Navigator* navigator = nullptr;
    platform::GamepadInput input;
    ui::InputRouter router;
  };

} // namespace

TEST_CASE("Gamepad Back leaves the page; Search opens the palette and Back closes it") {
  Scene scene;
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(pad.attached());
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 1; }));

  // A page that is not a sidebar root, on the placeholder.
  REQUIRE(scene.navigator->navigate(QStringLiteral("customizeHome")));
  pump(50);
  REQUIRE(scene.navigator->depth() == 2);
  scene.tap(pad, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(scene.navigator->depth() == 1);
  CHECK(scene.navigator->currentName() == QStringLiteral("home"));

  scene.tap(pad, SDL_GAMEPAD_BUTTON_NORTH);
  CHECK(scene.palette->property("opened").toBool());
  scene.tap(pad, SDL_GAMEPAD_BUTTON_EAST);
  CHECK_FALSE(scene.palette->property("opened").toBool());
  // Closing the palette is not a step back.
  CHECK(scene.navigator->currentName() == QStringLiteral("home"));
}

namespace {

  // The shell in TV mode on a page of 60 rows, driven by the gamepad alone;
  // the presses go through the button check first, as in InputInstall.cpp.
  struct TvScene {
    TvScene() {
      REQUIRE(pages.isValid());
      const QString page = pages.filePath(QStringLiteral("RowsPage.qml"));
      QFile file(page);
      REQUIRE(file.open(QIODevice::WriteOnly));
      file.write(R"(
import QtQuick
import Jfd.Ui.Controls
import Jfd.Ui.Shell
RoutePage {
  ListView {
    objectName: "rows"
    anchors.fill: parent
    clip: true
    focus: true
    model: 60
    delegate: ListItem { required property int index; objectName: "row" + index; width: ListView.view.width; text: "Row " + index }
  }
}
)");
      file.close();
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      status = engine.singletonInstance<ui::GamepadStatus*>("Jfd.Ui.Shell", "GamepadStatus");
      REQUIRE(navigator != nullptr);
      REQUIRE(status != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      navigator->registerPage(QStringLiteral("home"), QUrl::fromLocalFile(page));
      navigator->reset(QStringLiteral("home"));
      window.resize(1920, 1080);
      QQmlComponent component(&engine);
      component.setData(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
AppShell { Component.onCompleted: Theme.tvMode = true }
)",
                        QUrl(QStringLiteral("qrc:/gamepad-tv.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      auto* shell = qobject_cast<QQuickItem*>(object.get());
      shell->setParentItem(window.contentItem());
      shell->setSize(QSizeF(1920, 1080));
      window.show();
      pump(300);

      router.setWindow(&window);
      QObject::connect(&input, &platform::GamepadInput::actionTriggered, &router,
                       [this](platform::InputAction action, bool repeat) {
                         if (!status->takePress(action, repeat)) router.route(action, repeat);
                       });
      REQUIRE(sdl.ok);
      REQUIRE(input.start());
    }

    QString focused() const {
      const QQuickItem* item = window.activeFocusItem();
      return item ? item->objectName() : QString();
    }

    void tap(VirtualGamepad& pad, int button) {
      int actions = 0;
      auto connection = QObject::connect(&input, &platform::GamepadInput::actionTriggered, [&] { ++actions; });
      pad.button(button, true);
      REQUIRE(pumpUntil([&] { return actions > 0; }));
      pad.button(button, false);
      pump(40);
      QObject::disconnect(connection);
    }

    QTemporaryDir pages;
    SdlGamepadSubsystem sdl;
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> object;
    ui::Navigator* navigator = nullptr;
    ui::GamepadStatus* status = nullptr;
    platform::GamepadInput input;
    ui::InputRouter router;
  };

} // namespace

TEST_CASE("TV mode with a gamepad alone: d-pad, pages on the shoulder buttons, View to the sidebar, the button check") {
  TvScene scene;
  auto pad = VirtualGamepad::gamepad(0x2dc8, 0x6006, "8BitDo Pro 2");
  REQUIRE(pad.attached());
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 1; }));

  REQUIRE(pumpUntil([&] { return scene.focused() == QStringLiteral("row0"); }));
  scene.tap(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  CHECK(scene.focused() == QStringLiteral("row1"));
  // RB: a page down the list; LB: back up.
  scene.tap(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  const int paged = scene.focused().mid(3).toInt();
  CHECK(paged >= 6);
  scene.tap(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  CHECK(scene.focused().mid(3).toInt() < paged);

  // Left: the sidebar; View (Menu): back to the page and to the sidebar.
  scene.tap(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  CHECK(scene.focused().startsWith(QLatin1String("nav_")));
  scene.tap(pad, SDL_GAMEPAD_BUTTON_BACK);
  CHECK(scene.focused().startsWith(QLatin1String("row")));
  scene.tap(pad, SDL_GAMEPAD_BUTTON_BACK);
  CHECK(scene.focused() == QStringLiteral("nav_home"));
  scene.tap(pad, SDL_GAMEPAD_BUTTON_BACK);
  const QString onPage = scene.focused();
  CHECK(onPage.startsWith(QLatin1String("row")));

  // The button check: presses show, nothing moves; B twice ends it.
  scene.status->setAvailable(true);
  scene.status->setControllers(scene.input.controllerNames());
  scene.status->startCheck();
  pump(200);
  QObject* dialog = scene.object->findChild<QObject*>(QStringLiteral("gamepadCheck"));
  REQUIRE(dialog != nullptr);
  CHECK(dialog->property("visible").toBool());
  scene.tap(pad, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(scene.status->lastAction() == QStringLiteral("Select"));
  scene.tap(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  CHECK(scene.status->lastAction() == QStringLiteral("Down"));
  scene.tap(pad, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(scene.status->checking());
  scene.tap(pad, SDL_GAMEPAD_BUTTON_EAST);
  CHECK_FALSE(scene.status->checking());
  pump(300);
  CHECK_FALSE(dialog->property("visible").toBool());
  CHECK(scene.navigator->currentName() == QStringLiteral("home"));
  // Back on the page where the check left it.
  scene.tap(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  CHECK(scene.focused().startsWith(QLatin1String("row")));
  CHECK(scene.focused() != onPage);
}
