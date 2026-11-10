#include "AppServices.h"
#include "IntegrationServices.h"

#include <jfd/integrations/MediaSessionBridge.h>
#include <jfd/platform/GamepadInput.h>
#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>
#include <jfd/ui/InputRouter.h>

#include <QQuickWindow>

namespace jfd::desktop {

  namespace {
    const QString GamepadKey = QStringLiteral("input/gamepad");
    const QString BackgroundKey = QStringLiteral("input/gamepadInBackground");
    // The player's own arrow-key step.
    constexpr qint64 SeekStepMs = 10'000;

    // PlayPause and the seek triggers: the media controls' path, so in a
    // group they are requests to the group and they work on every screen.
    void mediaAction(AppServices& services, platform::InputAction action) {
      if (!services.integrations || !services.integrations->bridge) return;
      integrations::MediaSessionBridge& bridge = *services.integrations->bridge;
      switch (action) {
      case platform::InputAction::PlayPause: bridge.requestCommand(platform::MediaCommand::PlayPause); break;
      case platform::InputAction::SeekForward: bridge.seekBy(SeekStepMs); break;
      case platform::InputAction::SeekBack: bridge.seekBy(-SeekStepMs); break;
      default: break;
      }
    }
  } // namespace

  bool registerInputSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("input");
    section.context = "InputSettings";
    section.title = QT_TRANSLATE_NOOP("InputSettings", "Gamepad");
    section.order = 120;
    section.hidden = !platform::GamepadInput::isSupported();

    settings::SettingDescriptor gamepad;
    gamepad.key = QStringLiteral("gamepad");
    gamepad.type = settings::SettingType::Bool;
    gamepad.defaultValue = true;
    gamepad.label = QT_TRANSLATE_NOOP("InputSettings", "Use a gamepad");
    gamepad.description = QT_TRANSLATE_NOOP(
        "InputSettings",
        "Xbox, PlayStation, Switch and 8BitDo controllers. The stick and the d-pad move, the bottom button "
        "selects, the right one goes back, Start pauses, the triggers seek.");
    section.settings.append(gamepad);

    settings::SettingDescriptor background;
    background.key = QStringLiteral("gamepadInBackground");
    background.type = settings::SettingType::Bool;
    background.defaultValue = false;
    background.label = QT_TRANSLATE_NOOP("InputSettings", "Gamepad works in the background");
    background.description = QT_TRANSLATE_NOOP(
        "InputSettings", "Off: the gamepad controls the app only while its window is active.");
    section.settings.append(background);

    return registry.registerSection(std::move(section));
  }

  void installInput(AppServices& services, QQuickWindow& window) {
    if (!platform::GamepadInput::isSupported()) return;
    // Both live as long as the window they serve.
    auto* gamepad = new platform::GamepadInput(&window);
    auto* router = new ui::InputRouter(&window);
    router->setWindow(&window);
    router->setMediaHandler([&services](platform::InputAction action) { mediaAction(services, action); });
    QObject::connect(gamepad, &platform::GamepadInput::actionTriggered, router, &ui::InputRouter::route);

    settings::Settings& settings = *services.settings;
    // Only while the window is active, unless allowed in the background.
    auto follow = [gamepad, &window, &settings] {
      gamepad->setAcceptsInput(window.isActive() || settings.value(BackgroundKey).toBool());
    };
    auto enable = [gamepad, &settings] {
      if (settings.value(GamepadKey).toBool())
        gamepad->start();
      else
        gamepad->stop();
    };
    QObject::connect(&window, &QWindow::activeChanged, gamepad, follow);
    QObject::connect(&settings, &settings::Settings::valueChanged, gamepad,
                     [follow, enable](const QString& key, const QVariant&) {
                       if (key == BackgroundKey) follow();
                       if (key == GamepadKey) enable();
                     });
    follow();
    enable();
  }

} // namespace jfd::desktop
