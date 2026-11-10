#pragma once

#include <QMetaType>

#include <string_view>

// What a remote-like input asks the interface to do, independent of where it
// came from: a gamepad (GamepadInput), and later the server's navigation
// commands. The shell turns navigation actions into keys for the focused
// window (ui::InputRouter); the media actions go to the playback session the
// way the system media controls do, so they work on every screen and in a
// SyncPlay group.
namespace jfd::platform {

  enum class InputAction {
    Up,
    Down,
    Left,
    Right,
    Select,
    Back,
    Menu,
    PlayPause,
    SeekForward,
    SeekBack,
    PageLeft,
    PageRight,
    Search,
  };

  inline constexpr int InputActionCount = static_cast<int>(InputAction::Search) + 1;

  // PlayPause, SeekForward and SeekBack: not keys, session commands.
  constexpr bool isMediaAction(InputAction action) {
    return action == InputAction::PlayPause || action == InputAction::SeekForward || action == InputAction::SeekBack;
  }

  // Held down, these repeat (400 ms, then 8 per second); the others act once
  // per press.
  constexpr bool repeatsWhenHeld(InputAction action) {
    switch (action) {
    case InputAction::Up:
    case InputAction::Down:
    case InputAction::Left:
    case InputAction::Right:
    case InputAction::SeekForward:
    case InputAction::SeekBack:
    case InputAction::PageLeft:
    case InputAction::PageRight: return true;
    case InputAction::Select:
    case InputAction::Back:
    case InputAction::Menu:
    case InputAction::PlayPause:
    case InputAction::Search: return false;
    }
    return false;
  }

  // For logs and tests.
  constexpr std::string_view inputActionName(InputAction action) {
    switch (action) {
    case InputAction::Up: return "Up";
    case InputAction::Down: return "Down";
    case InputAction::Left: return "Left";
    case InputAction::Right: return "Right";
    case InputAction::Select: return "Select";
    case InputAction::Back: return "Back";
    case InputAction::Menu: return "Menu";
    case InputAction::PlayPause: return "PlayPause";
    case InputAction::SeekForward: return "SeekForward";
    case InputAction::SeekBack: return "SeekBack";
    case InputAction::PageLeft: return "PageLeft";
    case InputAction::PageRight: return "PageRight";
    case InputAction::Search: return "Search";
    }
    return "?";
  }

} // namespace jfd::platform

Q_DECLARE_METATYPE(jfd::platform::InputAction)
