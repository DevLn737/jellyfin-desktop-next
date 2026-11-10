// The gamepad on the real shell: Back (B on Xbox) leaves the page through the
// shell's Escape handling, Search (Y) opens the Ctrl+K palette and Back
// closes it again. SDL's virtual joystick → GamepadInput → InputRouter.
#include "../support/DoctestQt.h"
#include "../platform/VirtualGamepad.h"

#include <jfd/platform/GamepadInput.h>
#include <jfd/ui/InputRouter.h>
#include <jfd/ui/Navigator.h>

#include <QElapsedTimer>
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
