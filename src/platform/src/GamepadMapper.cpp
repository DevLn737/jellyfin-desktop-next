#include <jfd/platform/GamepadMapper.h>

#include <algorithm>
#include <cmath>

namespace jfd::platform {

  std::optional<InputAction> defaultAction(GamepadControl control) {
    switch (control) {
    case GamepadControl::DpadUp:
    case GamepadControl::StickUp: return InputAction::Up;
    case GamepadControl::DpadDown:
    case GamepadControl::StickDown: return InputAction::Down;
    case GamepadControl::DpadLeft:
    case GamepadControl::StickLeft: return InputAction::Left;
    case GamepadControl::DpadRight:
    case GamepadControl::StickRight: return InputAction::Right;
    case GamepadControl::South: return InputAction::Select;
    case GamepadControl::East: return InputAction::Back;
    case GamepadControl::Start: return InputAction::PlayPause;
    case GamepadControl::Back: return InputAction::Menu;
    case GamepadControl::North: return InputAction::Search;
    case GamepadControl::LeftShoulder: return InputAction::PageLeft;
    case GamepadControl::RightShoulder: return InputAction::PageRight;
    case GamepadControl::LeftTrigger: return InputAction::SeekBack;
    case GamepadControl::RightTrigger: return InputAction::SeekForward;
    case GamepadControl::West: return std::nullopt;
    }
    return std::nullopt;
  }

  GamepadMapper::GamepadMapper(Emit emit) : m_emit(std::move(emit)) {}

  bool GamepadMapper::isHeld(PadId pad, GamepadControl control) const {
    return std::find(m_held.begin(), m_held.end(), Held{pad, control}) != m_held.end();
  }

  void GamepadMapper::press(PadId pad, GamepadControl control, Millis now) {
    const Held held{pad, control};
    if (isHeld(pad, control)) return;
    m_held.push_back(held);
    const auto action = defaultAction(control);
    if (!action) return;
    // The newest press repeats, as on a keyboard; an earlier one held
    // alongside stops repeating.
    if (repeatsWhenHeld(*action)) {
      m_repeat = held;
      m_nextRepeat = now + RepeatDelay;
    } else {
      m_repeat.reset();
    }
    if (m_emit) m_emit(*action, false);
  }

  void GamepadMapper::release(PadId pad, GamepadControl control) {
    const Held held{pad, control};
    m_held.erase(std::remove(m_held.begin(), m_held.end(), held), m_held.end());
    if (m_repeat == held) m_repeat.reset();
  }

  void GamepadMapper::stick(PadId pad, double x, double y, Millis now) {
    PadAxes& axes = m_axes[pad];
    std::optional<GamepadControl> direction;
    if (std::max(std::abs(x), std::abs(y)) > StickDeadZone) {
      // The axis further out decides, so a slightly diagonal push moves one
      // step, not two.
      if (std::abs(x) >= std::abs(y))
        direction = x < 0 ? GamepadControl::StickLeft : GamepadControl::StickRight;
      else
        direction = y < 0 ? GamepadControl::StickUp : GamepadControl::StickDown;
    }
    if (direction == axes.stick) return;
    if (axes.stick) release(pad, *axes.stick);
    axes.stick = direction;
    if (direction) press(pad, *direction, now);
  }

  void GamepadMapper::trigger(PadId pad, bool left, double value, Millis now) {
    PadAxes& axes = m_axes[pad];
    bool& down = left ? axes.leftTrigger : axes.rightTrigger;
    const GamepadControl control = left ? GamepadControl::LeftTrigger : GamepadControl::RightTrigger;
    if (!down && value >= TriggerPress) {
      down = true;
      press(pad, control, now);
    } else if (down && value < TriggerRelease) {
      down = false;
      release(pad, control);
    }
  }

  void GamepadMapper::releasePad(PadId pad) {
    m_held.erase(std::remove_if(m_held.begin(), m_held.end(), [pad](const Held& held) { return held.pad == pad; }),
                 m_held.end());
    if (m_repeat && m_repeat->pad == pad) m_repeat.reset();
    m_axes.erase(pad);
  }

  void GamepadMapper::releaseAll() {
    m_held.clear();
    m_repeat.reset();
    m_axes.clear();
  }

  void GamepadMapper::tick(Millis now) {
    if (!m_repeat || now < m_nextRepeat) return;
    const auto action = defaultAction(m_repeat->control);
    if (!action) return;
    // One repeat per tick: a late timer does not send a burst.
    m_nextRepeat += RepeatInterval;
    if (m_nextRepeat <= now) m_nextRepeat = now + RepeatInterval;
    if (m_emit) m_emit(*action, true);
  }

} // namespace jfd::platform
