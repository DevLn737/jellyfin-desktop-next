// GamepadInput on SDL's virtual joystick: the SDL path from a button to an
// InputAction, without hardware (CI). Xbox, and 8BitDo in its three modes:
// XInput (an Xbox 360 pad to the system), Switch (a Switch Pro Controller,
// Nintendo button layout) and DirectInput (a plain joystick that SDL's
// mapping database turns into a gamepad).
#include "../support/DoctestQt.h"
#include "VirtualGamepad.h"
#include "Wait.h"

#include <jfd/platform/GamepadInput.h>

#include <QElapsedTimer>
#include <QSignalSpy>

#include <string>
#include <vector>

using jfd::platform::GamepadInput;
using jfd::platform::InputAction;
using jfd::test::SdlGamepadSubsystem;
using jfd::test::spinUntil;
using jfd::test::VirtualGamepad;

namespace {
  struct Recorded {
    InputAction action;
    bool repeat;
    qint64 atMs;
  };

  // Records GamepadInput's actions with the time they came.
  struct Recorder {
    explicit Recorder(GamepadInput& input) {
      clock.start();
      QObject::connect(&input, &GamepadInput::actionTriggered, &input, [this](InputAction action, bool repeat) {
        events.push_back({action, repeat, clock.elapsed()});
      });
    }
    std::vector<InputAction> actions() const {
      std::vector<InputAction> result;
      for (const auto& event : events) result.push_back(event.action);
      return result;
    }
    int repeats() const {
      int count = 0;
      for (const auto& event : events) count += event.repeat ? 1 : 0;
      return count;
    }
    QElapsedTimer clock;
    std::vector<Recorded> events;
  };

  // Lets a few polls (8 ms each) pass.
  void settle(int ms = 40) {
    QElapsedTimer timer;
    timer.start();
    spinUntil([&] { return timer.elapsed() >= ms; }, ms + 1000);
  }

  // Presses and releases a button; true when it gave exactly one action.
  bool tap(VirtualGamepad& pad, Recorder& recorder, int button) {
    const auto before = recorder.events.size();
    pad.button(button, true);
    settle();
    pad.button(button, false);
    settle();
    return recorder.events.size() == before + 1;
  }

  bool connect(GamepadInput& input, int count) {
    return spinUntil([&] { return input.connectedCount() == count; }, 3000);
  }

  // The positional layout, checked on any gamepad.
  void checkLayout(VirtualGamepad& pad, Recorder& recorder) {
    struct Row {
      int button;
      InputAction action;
    };
    const std::vector<Row> rows = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, InputAction::Up},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, InputAction::Down},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, InputAction::Left},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, InputAction::Right},
        {SDL_GAMEPAD_BUTTON_SOUTH, InputAction::Select},
        {SDL_GAMEPAD_BUTTON_EAST, InputAction::Back},
        {SDL_GAMEPAD_BUTTON_NORTH, InputAction::Search},
        {SDL_GAMEPAD_BUTTON_START, InputAction::PlayPause},
        {SDL_GAMEPAD_BUTTON_BACK, InputAction::Menu},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, InputAction::PageLeft},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, InputAction::PageRight},
    };
    for (const auto& row : rows) {
      CAPTURE(row.button);
      REQUIRE(tap(pad, recorder, row.button));
      CHECK(recorder.events.back().action == row.action);
      CHECK_FALSE(recorder.events.back().repeat);
    }
    // West, the guide button and the stick clicks do nothing.
    const auto before = recorder.events.size();
    for (const int button : {SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_LEFT_STICK}) {
      pad.button(button, true);
      settle(20);
      pad.button(button, false);
    }
    settle();
    CHECK(recorder.events.size() == before);

    // Triggers: full travel seeks; a virtual trigger rests at the axis minimum.
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    settle();
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    settle();
    pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    settle();
    pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    settle();
    REQUIRE(recorder.events.size() == before + 2);
    CHECK(recorder.events[before].action == InputAction::SeekForward);
    CHECK(recorder.events[before + 1].action == InputAction::SeekBack);
  }
} // namespace

TEST_CASE("SDL gamepad input is built in") {
  CHECK(GamepadInput::isSupported());
  CHECK(GamepadInput::sdlVersion().startsWith(QStringLiteral("3.")));
}

TEST_CASE("Xbox controller: every button gives its action, by position") {
  SdlGamepadSubsystem sdl;
  REQUIRE(sdl.ok);
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  QSignalSpy changed(&input, &GamepadInput::controllersChanged);
  CHECK(input.pollInterval() == GamepadInput::IdlePollMs);

  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(pad.attached());
  REQUIRE(connect(input, 1));
  CHECK(changed.count() >= 1);
  CHECK(input.controllerNames() == QStringList{QStringLiteral("Xbox Series X Controller")});
  // Polled every 8 ms while a gamepad is there.
  CHECK(input.pollInterval() == GamepadInput::ActivePollMs);
  checkLayout(pad, recorder);

  pad.detach();
  REQUIRE(connect(input, 0));
  CHECK(input.pollInterval() == GamepadInput::IdlePollMs);
}

TEST_CASE("8BitDo in XInput mode is an Xbox 360 controller") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  auto pad = VirtualGamepad::gamepad(0x045e, 0x028e, "8BitDo Pro 2 (XInput)");
  REQUIRE(connect(input, 1));
  checkLayout(pad, recorder);
}

TEST_CASE("8BitDo in Switch mode: the bottom button selects although it is labelled B") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  // How 8BitDo controllers present themselves in Switch mode.
  auto pad = VirtualGamepad::gamepad(0x057e, 0x2009, "Nintendo Switch Pro Controller");
  REQUIRE(connect(input, 1));
  SDL_Gamepad* gamepad = SDL_OpenGamepad(pad.id());
  REQUIRE(gamepad != nullptr);
  CHECK(SDL_GetGamepadType(gamepad) == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO);
  CHECK(SDL_GetGamepadButtonLabel(gamepad, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_B);
  CHECK(SDL_GetGamepadButtonLabel(gamepad, SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_A);
  SDL_CloseGamepad(gamepad);
  checkLayout(pad, recorder);
}

TEST_CASE("8BitDo in DirectInput mode becomes a gamepad through its SDL mapping") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  // 8BitDo Pro 2 in D mode: a plain joystick with 15 buttons, 6 axes and a hat.
  auto pad = VirtualGamepad::joystick(0x2dc8, 0x6006, "8BitDo Pro 2", 15, 6);
  REQUIRE(pad.attached());
  settle(100);
  // No mapping, no gamepad.
  CHECK(input.connectedCount() == 0);

  // The 8BitDo Pro 2 line of SDL's database (src/joystick/SDL_gamepad_db.h),
  // with the virtual device's GUID: Nintendo layout, so the bottom button is
  // raw button 1 and the right one raw button 0.
  const std::string mapping =
      pad.guid() +
      ",8BitDo Pro 2,a:b1,b:b0,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,"
      "leftshoulder:b6,leftstick:b13,lefttrigger:b8,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b14,"
      "righttrigger:b9,rightx:a3,righty:a4,start:b11,x:b4,y:b3,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,";
  REQUIRE(SDL_AddGamepadMapping(mapping.c_str()) == 1);
  REQUIRE(connect(input, 1));

  struct Row {
    int button;
    InputAction action;
  };
  for (const Row& row : {Row{1, InputAction::Select}, Row{0, InputAction::Back}, Row{11, InputAction::PlayPause},
                         Row{10, InputAction::Menu}, Row{3, InputAction::Search}, Row{6, InputAction::PageLeft},
                         Row{7, InputAction::PageRight}, Row{8, InputAction::SeekBack},
                         Row{9, InputAction::SeekForward}}) {
    CAPTURE(row.button);
    REQUIRE(tap(pad, recorder, row.button));
    CHECK(recorder.events.back().action == row.action);
  }
  // The d-pad is the hat.
  pad.hat(SDL_HAT_UP);
  settle();
  pad.hat(SDL_HAT_CENTERED);
  settle();
  pad.hat(SDL_HAT_RIGHT);
  settle();
  pad.hat(SDL_HAT_CENTERED);
  settle();
  REQUIRE(recorder.events.size() >= 2);
  CHECK(recorder.events[recorder.events.size() - 2].action == InputAction::Up);
  CHECK(recorder.events.back().action == InputAction::Right);
}

TEST_CASE("Holding a direction repeats after 400 ms, then 8 times a second") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(connect(input, 1));

  pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
  REQUIRE(spinUntil([&] { return !recorder.events.empty(); }));
  const qint64 pressed = recorder.events.front().atMs;
  // 1100 ms held: the press, then repeats at 400, 525, 650, 775, 900, 1025.
  spinUntil([&] { return recorder.clock.elapsed() >= pressed + 1100; }, 3000);
  pad.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
  settle();
  const auto held = recorder.events.size();
  CHECK(recorder.repeats() >= 5);
  CHECK(recorder.repeats() <= 7);
  for (const auto& event : recorder.events) CHECK(event.action == InputAction::Down);
  REQUIRE(recorder.events.size() >= 3);
  const qint64 firstRepeat = recorder.events[1].atMs - pressed;
  CHECK(firstRepeat >= 390);
  CHECK(firstRepeat <= 480);
  const qint64 interval = recorder.events[2].atMs - recorder.events[1].atMs;
  CHECK(interval >= 100);
  CHECK(interval <= 160);

  // Released: no more.
  settle(500);
  CHECK(recorder.events.size() == held);
}

TEST_CASE("The left stick moves past a dead zone of 0.35") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(connect(input, 1));

  pad.axis(SDL_GAMEPAD_AXIS_LEFTX, static_cast<Sint16>(0.34 * 32767));
  settle();
  pad.axis(SDL_GAMEPAD_AXIS_LEFTY, static_cast<Sint16>(-0.34 * 32768));
  settle();
  CHECK(recorder.events.empty());

  pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0);
  pad.axis(SDL_GAMEPAD_AXIS_LEFTX, static_cast<Sint16>(0.40 * 32767));
  settle();
  pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
  settle();
  pad.axis(SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN);
  settle();
  pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0);
  settle();
  CHECK(recorder.actions() == std::vector<InputAction>{InputAction::Right, InputAction::Up});
}

TEST_CASE("A gamepad unplugged while a direction is held leaves nothing repeating") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(connect(input, 1));

  pad.button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
  pad.axis(SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MAX);
  REQUIRE(spinUntil([&] { return recorder.events.size() >= 2; }));
  pad.detach();
  REQUIRE(connect(input, 0));
  const auto count = recorder.events.size();
  settle(800);
  CHECK(recorder.events.size() == count);
  CHECK(recorder.repeats() == 0);
}

TEST_CASE("While the window is not active, presses are dropped") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(connect(input, 1));

  // Held when the window loses focus: the repeat stops.
  pad.button(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
  REQUIRE(spinUntil([&] { return recorder.events.size() == 1; }));
  input.setAcceptsInput(false);
  settle(700);
  CHECK(recorder.events.size() == 1);
  pad.button(SDL_GAMEPAD_BUTTON_DPAD_UP, false);

  CHECK_FALSE(tap(pad, recorder, SDL_GAMEPAD_BUTTON_SOUTH));
  pad.axis(SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);
  settle();
  pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
  settle();
  CHECK(recorder.events.size() == 1);

  input.setAcceptsInput(true);
  CHECK(tap(pad, recorder, SDL_GAMEPAD_BUTTON_SOUTH));
  CHECK(recorder.events.back().action == InputAction::Select);
}

TEST_CASE("Plugging in and out 20 times") {
  SdlGamepadSubsystem sdl;
  GamepadInput input;
  REQUIRE(input.start());
  Recorder recorder(input);
  QSignalSpy changed(&input, &GamepadInput::controllersChanged);
  for (int round = 0; round < 20; ++round) {
    CAPTURE(round);
    auto pad = VirtualGamepad::gamepad(0x2dc8, 0x6006, "8BitDo Pro 2");
    REQUIRE(connect(input, 1));
    pad.button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
    REQUIRE(spinUntil([&] { return recorder.events.size() == static_cast<std::size_t>(round + 1); }));
    pad.detach();
    REQUIRE(connect(input, 0));
  }
  CHECK(changed.count() >= 40);
  CHECK(recorder.repeats() == 0);
}

TEST_CASE("A gamepad connected before start is found, and stop closes it") {
  SdlGamepadSubsystem sdl;
  auto pad = VirtualGamepad::gamepad(0x045e, 0x0b12, "Xbox Series X Controller");
  REQUIRE(pad.attached());
  GamepadInput input;
  REQUIRE(input.start());
  CHECK(input.isRunning());
  REQUIRE(connect(input, 1));
  input.stop();
  CHECK_FALSE(input.isRunning());
  CHECK(input.connectedCount() == 0);
  // Started again, it finds the pad again.
  REQUIRE(input.start());
  REQUIRE(connect(input, 1));
}
