#pragma once

#include <jfd/platform/InputAction.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

// The gamepad layout and its timing, without SDL: which control gives which
// InputAction, the left stick's dead zone, the triggers' threshold and the
// repeat while a direction is held. GamepadInput feeds it SDL's events;
// tests feed it directly.
namespace jfd::platform {

  // Physical controls by position. SDL reports face buttons by where they
  // are, not by their label: South is A on an Xbox pad and B on a Nintendo
  // layout (8BitDo in Switch mode), so Select is always the bottom button.
  enum class GamepadControl {
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    South,
    East,
    West,
    North,
    Start,
    Back, // Back / View / Select / Share / Minus.
    LeftShoulder,
    RightShoulder,
    LeftTrigger,
    RightTrigger,
    StickUp,
    StickDown,
    StickLeft,
    StickRight,
  };

  // The default layout: d-pad and left stick → arrows, South → Select,
  // East → Back, Start → PlayPause, Back/View → Menu, North → Search,
  // shoulders → PageLeft/PageRight, triggers → SeekBack/SeekForward. West
  // does nothing.
  std::optional<InputAction> defaultAction(GamepadControl control);

  class GamepadMapper {
  public:
    using Millis = std::chrono::milliseconds;
    using PadId = std::uint32_t;
    using Emit = std::function<void(InputAction action, bool repeat)>;

    // A stick direction counts once the stick is further than this from the
    // centre (on the axis that is further out).
    static constexpr double StickDeadZone = 0.35;
    // Triggers press at half travel and release below 30 %.
    static constexpr double TriggerPress = 0.5;
    static constexpr double TriggerRelease = 0.3;
    static constexpr Millis RepeatDelay{400};
    static constexpr Millis RepeatInterval{125}; // 8 per second.

    explicit GamepadMapper(Emit emit);

    // A button went down or up. Pressing a held control again does nothing.
    void press(PadId pad, GamepadControl control, Millis now);
    void release(PadId pad, GamepadControl control);
    // The left stick, each axis from -1 (left, up) to 1 (right, down).
    void stick(PadId pad, double x, double y, Millis now);
    // A trigger, from 0 to 1.
    void trigger(PadId pad, bool left, double value, Millis now);
    // The pad is gone (or input is paused): what it held is released and
    // stops repeating.
    void releasePad(PadId pad);
    void releaseAll();
    // Emits the repeat that is due; called by the poll timer.
    void tick(Millis now);

    bool isRepeating() const { return m_repeat.has_value(); }
    bool isHeld(PadId pad, GamepadControl control) const;

  private:
    struct Held {
      PadId pad = 0;
      GamepadControl control = GamepadControl::South;
      bool operator==(const Held&) const = default;
    };
    struct PadAxes {
      std::optional<GamepadControl> stick;
      bool leftTrigger = false;
      bool rightTrigger = false;
    };

    Emit m_emit;
    std::vector<Held> m_held;
    std::optional<Held> m_repeat;
    Millis m_nextRepeat{0};
    std::map<PadId, PadAxes> m_axes;
  };

} // namespace jfd::platform
