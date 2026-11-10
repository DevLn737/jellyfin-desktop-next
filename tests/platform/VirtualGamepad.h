#pragma once

#include <SDL3/SDL.h>

#include <string>

// SDL's virtual joystick, standing in for a real controller in tests: the
// same SDL code path from SDL_UpdateJoysticks on, without hardware.
namespace jfd::test {

  // Holds SDL's gamepad subsystem for the test's lifetime, so virtual
  // joysticks can be attached before and after GamepadInput starts.
  class SdlGamepadSubsystem {
  public:
    SdlGamepadSubsystem() { ok = SDL_InitSubSystem(SDL_INIT_GAMEPAD); }
    ~SdlGamepadSubsystem() {
      if (ok) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
      if (SDL_WasInit(0) == 0) SDL_Quit();
    }
    SdlGamepadSubsystem(const SdlGamepadSubsystem&) = delete;
    SdlGamepadSubsystem& operator=(const SdlGamepadSubsystem&) = delete;
    bool ok = false;
  };

  class VirtualGamepad {
  public:
    // A controller SDL knows as a gamepad: buttons and axes are numbered as
    // SDL_GamepadButton and SDL_GamepadAxis (by position).
    static VirtualGamepad gamepad(Uint16 vendor, Uint16 product, const char* name) {
      SDL_VirtualJoystickDesc desc;
      SDL_INIT_INTERFACE(&desc);
      desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
      desc.vendor_id = vendor;
      desc.product_id = product;
      desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
      desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
      desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
      desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
      desc.name = name;
      return VirtualGamepad(desc);
    }

    // A plain joystick (a DirectInput-mode controller): raw buttons, axes
    // and one hat, a gamepad only once a mapping names its GUID.
    static VirtualGamepad joystick(Uint16 vendor, Uint16 product, const char* name, Uint16 buttons, Uint16 axes) {
      SDL_VirtualJoystickDesc desc;
      SDL_INIT_INTERFACE(&desc);
      desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
      desc.vendor_id = vendor;
      desc.product_id = product;
      desc.nbuttons = buttons;
      desc.naxes = axes;
      desc.nhats = 1;
      desc.name = name;
      return VirtualGamepad(desc);
    }

    VirtualGamepad(VirtualGamepad&& other) noexcept : m_id(other.m_id), m_joystick(other.m_joystick) {
      other.m_id = 0;
      other.m_joystick = nullptr;
    }
    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(VirtualGamepad&&) = delete;
    ~VirtualGamepad() { detach(); }

    bool attached() const { return m_joystick != nullptr; }
    SDL_JoystickID id() const { return m_id; }

    void button(int index, bool down) { SDL_SetJoystickVirtualButton(m_joystick, index, down); }
    void axis(int index, Sint16 value) { SDL_SetJoystickVirtualAxis(m_joystick, index, value); }
    void hat(Uint8 value) { SDL_SetJoystickVirtualHat(m_joystick, 0, value); }

    // The GUID text SDL's mapping strings start with.
    std::string guid() const {
      char text[64] = {};
      SDL_GUIDToString(SDL_GetJoystickGUIDForID(m_id), text, sizeof(text));
      return text;
    }

    // Unplugged: SDL sends "removed" at the next poll.
    void detach() {
      if (m_joystick) SDL_CloseJoystick(m_joystick);
      if (m_id != 0) SDL_DetachVirtualJoystick(m_id);
      m_joystick = nullptr;
      m_id = 0;
    }

  private:
    explicit VirtualGamepad(const SDL_VirtualJoystickDesc& desc) {
      m_id = SDL_AttachVirtualJoystick(&desc);
      if (m_id != 0) m_joystick = SDL_OpenJoystick(m_id);
    }

    SDL_JoystickID m_id = 0;
    SDL_Joystick* m_joystick = nullptr;
  };

} // namespace jfd::test
