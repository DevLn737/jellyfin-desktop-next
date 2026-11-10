#include <jfd/platform/GamepadInput.h>
#include <jfd/platform/GamepadMapper.h>

#include <QLoggingCategory>
#include <QTimer>

#include <SDL3/SDL.h>

#include <map>
#include <optional>

Q_LOGGING_CATEGORY(lcGamepad, "jfd.platform.gamepad")

namespace jfd::platform {

  namespace {
    std::optional<GamepadControl> controlFor(Uint8 button) {
      switch (static_cast<SDL_GamepadButton>(button)) {
      case SDL_GAMEPAD_BUTTON_SOUTH: return GamepadControl::South;
      case SDL_GAMEPAD_BUTTON_EAST: return GamepadControl::East;
      case SDL_GAMEPAD_BUTTON_WEST: return GamepadControl::West;
      case SDL_GAMEPAD_BUTTON_NORTH: return GamepadControl::North;
      case SDL_GAMEPAD_BUTTON_BACK: return GamepadControl::Back;
      case SDL_GAMEPAD_BUTTON_START: return GamepadControl::Start;
      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GamepadControl::LeftShoulder;
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GamepadControl::RightShoulder;
      case SDL_GAMEPAD_BUTTON_DPAD_UP: return GamepadControl::DpadUp;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GamepadControl::DpadDown;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GamepadControl::DpadLeft;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GamepadControl::DpadRight;
      default: return std::nullopt;
      }
    }

    // SDL's event times and SDL_GetTicks share one clock (since SDL_Init).
    GamepadMapper::Millis eventTime(const SDL_Event& event) {
      return GamepadMapper::Millis(static_cast<GamepadMapper::Millis::rep>(event.common.timestamp / 1'000'000));
    }

    GamepadMapper::Millis now() {
      return GamepadMapper::Millis(static_cast<GamepadMapper::Millis::rep>(SDL_GetTicks()));
    }

    double axisValue(Sint16 value) { return value < 0 ? value / 32768.0 : value / 32767.0; }
  } // namespace

  struct GamepadInput::Private {
    struct Pad {
      SDL_Gamepad* gamepad = nullptr;
      QString name;
      double x = 0.0;
      double y = 0.0;
    };

    explicit Private(GamepadInput* owner)
        : mapper([owner](InputAction action, bool repeat) {
            Q_EMIT owner->actionTriggered(action, repeat);
          }) {}

    // Opens a gamepad SDL reports; false when it is open already or fails.
    bool open(SDL_JoystickID id) {
      if (pads.count(id) != 0) return false;
      SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
      if (gamepad == nullptr) {
        qCWarning(lcGamepad) << "Cannot open a gamepad:" << SDL_GetError();
        return false;
      }
      Pad pad;
      pad.gamepad = gamepad;
      const char* name = SDL_GetGamepadName(gamepad);
      pad.name = name != nullptr ? QString::fromUtf8(name) : QStringLiteral("Gamepad");
      // The model only: no serial number or device path in the log.
      qCInfo(lcGamepad).nospace() << "Gamepad connected: " << pad.name << " ("
                                  << QString::asprintf("%04x:%04x", SDL_GetGamepadVendor(gamepad),
                                                       SDL_GetGamepadProduct(gamepad))
                                  << ", " << SDL_GetGamepadStringForType(SDL_GetGamepadType(gamepad)) << ")";
      pads.emplace(id, std::move(pad));
      return true;
    }

    GamepadMapper mapper;
    QTimer timer;
    std::map<SDL_JoystickID, Pad> pads;
    bool running = false;
    bool acceptsInput = true;
  };

  GamepadInput::GamepadInput(QObject* parent) : QObject(parent), d(std::make_unique<Private>(this)) {
    d->timer.setTimerType(Qt::PreciseTimer);
    connect(&d->timer, &QTimer::timeout, this, &GamepadInput::poll);
  }

  GamepadInput::~GamepadInput() {
    // No controllersChanged from a half-destroyed object.
    blockSignals(true);
    stop();
  }

  bool GamepadInput::isSupported() { return true; }

  QString GamepadInput::sdlVersion() {
    const int version = SDL_GetVersion();
    return QStringLiteral("%1.%2.%3")
        .arg(SDL_VERSIONNUM_MAJOR(version))
        .arg(SDL_VERSIONNUM_MINOR(version))
        .arg(SDL_VERSIONNUM_MICRO(version));
  }

  bool GamepadInput::start() {
    if (d->running) return true;
    // There is no SDL window to have focus; the owner filters by the Qt
    // window's activity instead (setAcceptsInput).
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      qCWarning(lcGamepad) << "Gamepads are not available: SDL could not start:" << SDL_GetError();
      return false;
    }
    d->running = true;
    qCInfo(lcGamepad) << "SDL" << sdlVersion() << "gamepad input started";
    // Input queued while stopped is old; gamepads connected already are
    // listed here, since SDL announces them only when its subsystem starts
    // (something else in the process may have kept it running).
    SDL_FlushEvents(SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED);
    bool found = false;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
      for (int i = 0; i < count; ++i) found = d->open(ids[i]) || found;
      SDL_free(ids);
    }
    if (found) Q_EMIT controllersChanged();
    poll();
    d->timer.start(pollInterval());
    return true;
  }

  void GamepadInput::stop() {
    if (!d->running) return;
    d->timer.stop();
    d->mapper.releaseAll();
    const bool hadPads = !d->pads.empty();
    for (auto& [id, pad] : d->pads) SDL_CloseGamepad(pad.gamepad);
    d->pads.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    // Nothing else in the process uses SDL (tests may): clean up all of it.
    if (SDL_WasInit(0) == 0) SDL_Quit();
    d->running = false;
    if (hadPads) Q_EMIT controllersChanged();
  }

  bool GamepadInput::isRunning() const { return d->running; }

  void GamepadInput::setAcceptsInput(bool accepts) {
    if (d->acceptsInput == accepts) return;
    d->acceptsInput = accepts;
    if (accepts) return;
    d->mapper.releaseAll();
    // Axis events are dropped meanwhile: start again from the centre.
    for (auto& [id, pad] : d->pads) pad.x = pad.y = 0.0;
  }

  bool GamepadInput::acceptsInput() const { return d->acceptsInput; }

  int GamepadInput::connectedCount() const { return static_cast<int>(d->pads.size()); }

  QStringList GamepadInput::controllerNames() const {
    QStringList names;
    for (const auto& [id, pad] : d->pads) names.append(pad.name);
    return names;
  }

  int GamepadInput::pollInterval() const { return d->pads.empty() ? IdlePollMs : ActivePollMs; }

  void GamepadInput::poll() {
    if (!d->running) return;
    const std::size_t padsBefore = d->pads.size();
    bool changed = false;
    SDL_Event event;
    // SDL_PollEvent pumps: it updates the joysticks and queues their events.
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_GAMEPAD_ADDED:
        if (d->open(event.gdevice.which)) changed = true;
        break;
      case SDL_EVENT_GAMEPAD_REMOVED: {
        const auto found = d->pads.find(event.gdevice.which);
        if (found == d->pads.end()) break;
        qCInfo(lcGamepad) << "Gamepad disconnected:" << found->second.name;
        // A direction held while the pad went away must not keep repeating.
        d->mapper.releasePad(found->first);
        SDL_CloseGamepad(found->second.gamepad);
        d->pads.erase(found);
        changed = true;
        break;
      }
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (d->pads.count(event.gbutton.which) == 0) break;
        const auto control = controlFor(event.gbutton.button);
        if (!control) break;
        if (!event.gbutton.down)
          d->mapper.release(event.gbutton.which, *control);
        else if (d->acceptsInput)
          d->mapper.press(event.gbutton.which, *control, eventTime(event));
        break;
      }
      case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const auto found = d->pads.find(event.gaxis.which);
        if (found == d->pads.end() || !d->acceptsInput) break;
        Private::Pad& pad = found->second;
        const double value = axisValue(event.gaxis.value);
        switch (static_cast<SDL_GamepadAxis>(event.gaxis.axis)) {
        case SDL_GAMEPAD_AXIS_LEFTX:
          pad.x = value;
          d->mapper.stick(found->first, pad.x, pad.y, eventTime(event));
          break;
        case SDL_GAMEPAD_AXIS_LEFTY:
          pad.y = value;
          d->mapper.stick(found->first, pad.x, pad.y, eventTime(event));
          break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
          d->mapper.trigger(found->first, true, value, eventTime(event));
          break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
          d->mapper.trigger(found->first, false, value, eventTime(event));
          break;
        default: break;
        }
        break;
      }
      default: break;
      }
      // A handler of actionTriggered may have stopped the input.
      if (!d->running) return;
    }
    if (d->acceptsInput) d->mapper.tick(now());
    if (d->pads.size() != padsBefore || changed) {
      if (d->timer.isActive()) d->timer.setInterval(pollInterval());
      Q_EMIT controllersChanged();
    }
  }

} // namespace jfd::platform
