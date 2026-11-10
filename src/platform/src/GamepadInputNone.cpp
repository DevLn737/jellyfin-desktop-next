// Builds without SDL (JFD_GAMEPAD off): no gamepads.
#include <jfd/platform/GamepadInput.h>

namespace jfd::platform {

  struct GamepadInput::Private {
    bool acceptsInput = true;
  };

  GamepadInput::GamepadInput(QObject* parent) : QObject(parent), d(std::make_unique<Private>()) {}
  GamepadInput::~GamepadInput() = default;

  bool GamepadInput::isSupported() { return false; }
  QString GamepadInput::sdlVersion() { return {}; }
  bool GamepadInput::start() { return false; }
  void GamepadInput::stop() {}
  bool GamepadInput::isRunning() const { return false; }
  void GamepadInput::setAcceptsInput(bool accepts) { d->acceptsInput = accepts; }
  bool GamepadInput::acceptsInput() const { return d->acceptsInput; }
  int GamepadInput::connectedCount() const { return 0; }
  QStringList GamepadInput::controllerNames() const { return {}; }
  int GamepadInput::pollInterval() const { return IdlePollMs; }
  void GamepadInput::poll() {}

} // namespace jfd::platform
