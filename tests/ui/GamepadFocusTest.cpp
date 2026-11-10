// A gamepad driving a Qt Quick window: SDL's virtual joystick → GamepadInput
// → InputRouter → key events on the window. Threshold "Gamepad: path without
// a mouse": from the SDL button to the change of activeFocusItem < 30 ms.
#include "../support/DoctestQt.h"
#include "../platform/VirtualGamepad.h"

#include <jfd/platform/GamepadInput.h>
#include <jfd/ui/InputRouter.h>

#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using jfd::platform::GamepadInput;
using jfd::platform::InputAction;
using jfd::test::SdlGamepadSubsystem;
using jfd::test::VirtualGamepad;
using jfd::ui::InputRouter;

namespace {

  QQuickItem* find(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  void pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
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

  // Five focusable rows; arrows move between them, the root counts the keys
  // nobody else took.
  constexpr auto SceneQml = R"(
import QtQuick
Item {
  id: root
  width: 400; height: 400
  property int returns: 0
  property int escapes: 0
  property int palettes: 0
  property int repeats: 0
  property int otherKeys: 0
  Column {
    Repeater {
      id: rows
      model: 5
      Rectangle {
        required property int index
        objectName: "row" + index
        width: 400; height: 60
        focus: index === 0
        activeFocusOnTab: true
        color: activeFocus ? "steelblue" : "gray"
        Keys.onPressed: (event) => {
          if (event.isAutoRepeat) root.repeats++
          if (event.key === Qt.Key_Down && index < 4) { rows.itemAt(index + 1).forceActiveFocus(); event.accepted = true }
          else if (event.key === Qt.Key_Up && index > 0) { rows.itemAt(index - 1).forceActiveFocus(); event.accepted = true }
          else if (event.key === Qt.Key_Return) { root.returns++; event.accepted = true }
        }
      }
    }
  }
  Keys.onPressed: (event) => {
    if (event.key === Qt.Key_Escape) root.escapes++
    else if (event.key === Qt.Key_K && (event.modifiers & Qt.ControlModifier)) root.palettes++
    else root.otherKeys++
    event.accepted = true
  }
}
)";

  struct Scene {
    Scene() {
      QQmlComponent component(&engine);
      component.setData(SceneQml, QUrl(QStringLiteral("qrc:/gamepad-scene.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      root = qobject_cast<QQuickItem*>(object.get());
      root->setParentItem(window.contentItem());
      window.resize(400, 400);
      window.show();
      window.requestActivate();
      pump(50);
      REQUIRE(row(0) != nullptr);
      row(0)->forceActiveFocus();
      REQUIRE(window.activeFocusItem() == row(0));

      router.setWindow(&window);
      router.setMediaHandler([this](InputAction action) { media.push_back(action); });
      QObject::connect(&input, &GamepadInput::actionTriggered, &router, &InputRouter::route);
      REQUIRE(sdl.ok);
      REQUIRE(input.start());
    }

    QQuickItem* row(int index) const { return find(root, QStringLiteral("row%1").arg(index)); }
    int count(const char* name) const { return root->property(name).toInt(); }

    // Presses a button and lets it go; waits until the action arrived.
    bool tap(VirtualGamepad& pad, int button) {
      int actions = 0;
      auto connection = QObject::connect(&input, &GamepadInput::actionTriggered, [&] { ++actions; });
      pad.button(button, true);
      const bool arrived = pumpUntil([&] { return actions > 0; });
      pad.button(button, false);
      pump(30);
      QObject::disconnect(connection);
      return arrived;
    }

    SdlGamepadSubsystem sdl;
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> object;
    QQuickItem* root = nullptr;
    GamepadInput input;
    InputRouter router;
    std::vector<InputAction> media;
  };

} // namespace

TEST_CASE("From a gamepad button to the focus change takes under 30 ms") {
  Scene scene;
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(pad.attached());
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 1; }));

  // Down four times to the last row, up four times, down twice.
  const std::vector<int> presses = {
      SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
      SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_UP,   SDL_GAMEPAD_BUTTON_DPAD_UP,
      SDL_GAMEPAD_BUTTON_DPAD_UP,   SDL_GAMEPAD_BUTTON_DPAD_UP,   SDL_GAMEPAD_BUTTON_DPAD_DOWN,
      SDL_GAMEPAD_BUTTON_DPAD_DOWN};
  const std::vector<int> expectedRow = {1, 2, 3, 4, 3, 2, 1, 0, 1, 2};
  std::vector<double> latencies;
  QElapsedTimer clock;
  qint64 changedAt = -1;
  QObject::connect(&scene.window, &QQuickWindow::activeFocusItemChanged, &scene.window,
                   [&] { changedAt = clock.nsecsElapsed(); });
  for (std::size_t i = 0; i < presses.size(); ++i) {
    CAPTURE(i);
    changedAt = -1;
    // Between polls, as a real press would come.
    pump(static_cast<int>(3 + i % 7));
    clock.start();
    pad.button(presses[i], true);
    REQUIRE(pumpUntil([&] { return changedAt >= 0; }));
    latencies.push_back(static_cast<double>(changedAt) / 1e6);
    CHECK(scene.window.activeFocusItem() == scene.row(expectedRow[i]));
    pad.button(presses[i], false);
    pump(20);
  }
  std::vector<double> sorted = latencies;
  std::sort(sorted.begin(), sorted.end());
  std::string all;
  for (const double ms : latencies) all += std::to_string(ms).substr(0, 5) + " ";
  MESSAGE("button to focus change, ms: " << all << "(median " << sorted[sorted.size() / 2] << ", max "
                                          << sorted.back() << ")");
  for (const double ms : latencies) CHECK(ms < 30.0);
  // No mouse, no keyboard: nothing reached the root.
  CHECK(scene.count("otherKeys") == 0);
}

TEST_CASE("Select is Return, Back is Escape, Search is Ctrl+K") {
  Scene scene;
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 1; }));

  REQUIRE(scene.tap(pad, SDL_GAMEPAD_BUTTON_SOUTH));
  CHECK(scene.count("returns") == 1);
  REQUIRE(scene.tap(pad, SDL_GAMEPAD_BUTTON_EAST));
  CHECK(scene.count("escapes") == 1);
  REQUIRE(scene.tap(pad, SDL_GAMEPAD_BUTTON_NORTH));
  CHECK(scene.count("palettes") == 1);
  CHECK(scene.count("otherKeys") == 0);
  CHECK(scene.media.empty());
}

TEST_CASE("PlayPause and the seek triggers go to the session, not to the focused item") {
  Scene scene;
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 1; }));

  REQUIRE(scene.tap(pad, SDL_GAMEPAD_BUTTON_START));
  pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
  REQUIRE(pumpUntil([&] { return scene.media.size() == 2; }));
  pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
  pump(30);
  pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
  REQUIRE(pumpUntil([&] { return scene.media.size() == 3; }));
  pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
  pump(30);
  CHECK(scene.media ==
        std::vector<InputAction>{InputAction::PlayPause, InputAction::SeekForward, InputAction::SeekBack});
  CHECK(scene.count("otherKeys") == 0);
  CHECK(scene.count("returns") == 0);
  CHECK(scene.window.activeFocusItem() == scene.row(0));
}

TEST_CASE("A held direction sends auto-repeated keys") {
  Scene scene;
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 1; }));

  pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
  // The press, then repeats at 400 and 525 ms: two more rows.
  REQUIRE(pumpUntil([&] { return scene.window.activeFocusItem() == scene.row(3); }, 1500));
  CHECK(scene.count("repeats") >= 2);
  pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
  pump(30);
  const int repeats = scene.count("repeats");
  pump(400);
  CHECK(scene.count("repeats") == repeats);
}

TEST_CASE("Unplugged while held: no key keeps repeating") {
  Scene scene;
  auto pad = VirtualGamepad::gamepad(0x2dc8, 0x6006, "8BitDo Pro 2");
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 1; }));
  pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
  REQUIRE(pumpUntil([&] { return scene.window.activeFocusItem() == scene.row(1); }));
  pad.detach();
  REQUIRE(pumpUntil([&] { return scene.input.connectedCount() == 0; }));
  pump(700);
  CHECK(scene.window.activeFocusItem() == scene.row(1));
  CHECK(scene.count("repeats") == 0);
}
