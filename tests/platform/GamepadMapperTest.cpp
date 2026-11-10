#include "../support/DoctestQt.h"

#include <jfd/platform/GamepadMapper.h>

#include <string>
#include <vector>

using jfd::platform::GamepadControl;
using jfd::platform::GamepadMapper;
using jfd::platform::InputAction;
using Millis = GamepadMapper::Millis;

namespace {
  struct Recorded {
    InputAction action;
    bool repeat;
  };

  struct Recorder {
    std::vector<Recorded> events;
    GamepadMapper mapper{[this](InputAction action, bool repeat) { events.push_back({action, repeat}); }};

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
    // Ticks every 8 ms, as the poll timer does with a gamepad connected.
    void run(Millis from, Millis to) {
      for (Millis t = from; t <= to; t += Millis(8)) mapper.tick(t);
    }
  };
} // namespace

TEST_CASE("Each control gives its action in the default layout") {
  struct Row {
    GamepadControl control;
    std::optional<InputAction> action;
  };
  const std::vector<Row> layout = {
      {GamepadControl::DpadUp, InputAction::Up},
      {GamepadControl::DpadDown, InputAction::Down},
      {GamepadControl::DpadLeft, InputAction::Left},
      {GamepadControl::DpadRight, InputAction::Right},
      {GamepadControl::StickUp, InputAction::Up},
      {GamepadControl::StickDown, InputAction::Down},
      {GamepadControl::StickLeft, InputAction::Left},
      {GamepadControl::StickRight, InputAction::Right},
      {GamepadControl::South, InputAction::Select},
      {GamepadControl::East, InputAction::Back},
      {GamepadControl::West, std::nullopt},
      {GamepadControl::North, InputAction::Search},
      {GamepadControl::Start, InputAction::PlayPause},
      {GamepadControl::Back, InputAction::Menu},
      {GamepadControl::LeftShoulder, InputAction::PageLeft},
      {GamepadControl::RightShoulder, InputAction::PageRight},
      {GamepadControl::LeftTrigger, InputAction::SeekBack},
      {GamepadControl::RightTrigger, InputAction::SeekForward},
  };
  for (const auto& row : layout) {
    CAPTURE(static_cast<int>(row.control));
    CHECK(jfd::platform::defaultAction(row.control) == row.action);
    Recorder recorder;
    recorder.mapper.press(1, row.control, Millis(0));
    recorder.mapper.release(1, row.control);
    if (row.action) {
      REQUIRE(recorder.events.size() == 1);
      CHECK(recorder.events[0].action == *row.action);
      CHECK_FALSE(recorder.events[0].repeat);
    } else {
      CHECK(recorder.events.empty());
    }
  }
}

TEST_CASE("A held direction repeats after 400 ms at 8 per second, and stops on release") {
  Recorder recorder;
  recorder.mapper.press(1, GamepadControl::DpadDown, Millis(1000));
  recorder.run(Millis(1000), Millis(1392));
  CHECK(recorder.events.size() == 1);
  recorder.run(Millis(1400), Millis(1400));
  CHECK(recorder.events.size() == 2);
  // One second more: 8 repeats.
  recorder.run(Millis(1408), Millis(2400));
  CHECK(recorder.repeats() == 9);
  for (const auto action : recorder.actions()) CHECK(action == InputAction::Down);

  recorder.mapper.release(1, GamepadControl::DpadDown);
  const auto count = recorder.events.size();
  recorder.run(Millis(2408), Millis(4000));
  CHECK(recorder.events.size() == count);
  CHECK_FALSE(recorder.mapper.isRepeating());
}

TEST_CASE("Select, Back and PlayPause do not repeat") {
  for (const auto control : {GamepadControl::South, GamepadControl::East, GamepadControl::Start,
                             GamepadControl::Back, GamepadControl::North}) {
    Recorder recorder;
    recorder.mapper.press(1, control, Millis(0));
    recorder.run(Millis(0), Millis(2000));
    CHECK(recorder.events.size() == 1);
  }
}

TEST_CASE("A late timer sends one repeat, not a burst") {
  Recorder recorder;
  recorder.mapper.press(1, GamepadControl::DpadRight, Millis(0));
  recorder.mapper.tick(Millis(2000));
  CHECK(recorder.repeats() == 1);
  recorder.mapper.tick(Millis(2010));
  CHECK(recorder.repeats() == 1);
  recorder.mapper.tick(Millis(2125));
  CHECK(recorder.repeats() == 2);
}

TEST_CASE("The newest held direction repeats") {
  Recorder recorder;
  recorder.mapper.press(1, GamepadControl::DpadDown, Millis(0));
  recorder.mapper.press(1, GamepadControl::DpadRight, Millis(100));
  recorder.run(Millis(100), Millis(500));
  CHECK(recorder.actions() == std::vector<InputAction>{InputAction::Down, InputAction::Right, InputAction::Right});
  // Releasing the older one keeps the newer one repeating.
  recorder.mapper.release(1, GamepadControl::DpadDown);
  recorder.run(Millis(508), Millis(625));
  CHECK(recorder.events.back().action == InputAction::Right);
  CHECK(recorder.mapper.isRepeating());
}

TEST_CASE("Pressing a held button again does nothing") {
  Recorder recorder;
  recorder.mapper.press(1, GamepadControl::South, Millis(0));
  recorder.mapper.press(1, GamepadControl::South, Millis(10));
  CHECK(recorder.events.size() == 1);
  recorder.mapper.release(1, GamepadControl::South);
  recorder.mapper.press(1, GamepadControl::South, Millis(20));
  CHECK(recorder.events.size() == 2);
}

TEST_CASE("The left stick's dead zone is 0.35, and the axis further out decides") {
  Recorder recorder;
  recorder.mapper.stick(1, 0.34, 0.0, Millis(0));
  recorder.mapper.stick(1, 0.35, 0.0, Millis(1));
  recorder.mapper.stick(1, 0.0, -0.35, Millis(2));
  CHECK(recorder.events.empty());

  recorder.mapper.stick(1, 0.36, 0.0, Millis(3));
  CHECK(recorder.actions() == std::vector<InputAction>{InputAction::Right});
  // Further right: still the same push.
  recorder.mapper.stick(1, 1.0, 0.2, Millis(4));
  CHECK(recorder.events.size() == 1);
  // Mostly down now.
  recorder.mapper.stick(1, 0.4, 0.9, Millis(5));
  CHECK(recorder.actions() == std::vector<InputAction>{InputAction::Right, InputAction::Down});
  CHECK_FALSE(recorder.mapper.isHeld(1, GamepadControl::StickRight));
  // Back to the centre: released, no repeat.
  recorder.mapper.stick(1, 0.1, 0.1, Millis(6));
  CHECK_FALSE(recorder.mapper.isHeld(1, GamepadControl::StickDown));
  recorder.run(Millis(6), Millis(1000));
  CHECK(recorder.events.size() == 2);

  recorder.mapper.stick(1, -0.8, 0.0, Millis(1000));
  recorder.mapper.stick(1, 0.0, -0.8, Millis(1001));
  CHECK(recorder.actions() == std::vector<InputAction>{InputAction::Right, InputAction::Down, InputAction::Left,
                                                       InputAction::Up});
}

TEST_CASE("Triggers press at half travel and release below 30 %") {
  Recorder recorder;
  recorder.mapper.trigger(1, false, 0.49, Millis(0));
  CHECK(recorder.events.empty());
  recorder.mapper.trigger(1, false, 0.5, Millis(1));
  CHECK(recorder.actions() == std::vector<InputAction>{InputAction::SeekForward});
  recorder.mapper.trigger(1, false, 0.35, Millis(2));
  CHECK(recorder.mapper.isHeld(1, GamepadControl::RightTrigger));
  recorder.mapper.trigger(1, false, 0.2, Millis(3));
  CHECK_FALSE(recorder.mapper.isHeld(1, GamepadControl::RightTrigger));
  recorder.mapper.trigger(1, true, 1.0, Millis(4));
  CHECK(recorder.actions() == std::vector<InputAction>{InputAction::SeekForward, InputAction::SeekBack});
}

TEST_CASE("A pad that goes away while a direction is held stops repeating") {
  Recorder recorder;
  recorder.mapper.press(1, GamepadControl::DpadUp, Millis(0));
  recorder.mapper.stick(2, 0.0, 0.9, Millis(0));
  recorder.mapper.releasePad(2);
  // Pad 2 held the newest direction; its repeat is gone, pad 1 still holds.
  recorder.run(Millis(0), Millis(1000));
  CHECK(recorder.repeats() == 0);
  CHECK(recorder.mapper.isHeld(1, GamepadControl::DpadUp));
  CHECK_FALSE(recorder.mapper.isHeld(2, GamepadControl::StickDown));

  recorder.mapper.releasePad(1);
  CHECK_FALSE(recorder.mapper.isHeld(1, GamepadControl::DpadUp));
  // The stick of a pad that came back starts from the centre.
  recorder.mapper.stick(2, 0.0, 0.9, Millis(1100));
  CHECK(recorder.events.back().action == InputAction::Down);
}

TEST_CASE("releaseAll stops everything") {
  Recorder recorder;
  recorder.mapper.press(1, GamepadControl::DpadLeft, Millis(0));
  recorder.mapper.releaseAll();
  recorder.run(Millis(0), Millis(1000));
  CHECK(recorder.events.size() == 1);
  CHECK_FALSE(recorder.mapper.isRepeating());
}
