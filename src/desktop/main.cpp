#include "AppServices.h"
#include "IntegrationServices.h"
#include "Logging.h"
#include "FlowCheck.h"
#include "SmokeTest.h"
#include "Translations.h"

#include <jfd/integrations/IntegrationsSettings.h>
#include <jfd/platform/SingleInstance.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/settings/CommonSections.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/PlayerModel.h>
#include <jfd/ui/PlayerSession.h>
#include <jfd/ui/WindowStateKeeper.h>

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <memory>
#include <optional>

// The QML modules are static libraries linked into the executable.
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_SettingsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_GroupPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_PlayerPlugin)

using namespace jfd;

namespace {

  struct Options {
    bool smoke = false;
    // --flow: the smoke test's server path (FlowCheck); implies --smoke.
    bool flow = false;
    QString settingsFile;
  };

  // Before QGuiApplication: the interface scale has to be set first. Only
  // the options needed that early are read here; QCommandLineParser checks
  // the full command line afterwards.
  Options earlyOptions(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
      const QString arg = QString::fromLocal8Bit(argv[i]);
      if (arg == QLatin1String("--smoke")) {
        options.smoke = true;
      } else if (arg == QLatin1String("--flow")) {
        options.smoke = true;
        options.flow = true;
      } else if (arg == QLatin1String("--settings-file") && i + 1 < argc) {
        options.settingsFile = QString::fromLocal8Bit(argv[++i]);
      } else if (arg.startsWith(QLatin1String("--settings-file="))) {
        options.settingsFile = arg.section(QLatin1Char('='), 1);
      }
    }
    return options;
  }

  // Each module's settings section, before the file opens.
  bool registerSettings(settings::SettingsRegistry& registry) {
    bool ok = settings::registerCommonSections(registry);
    ok = desktop::registerLibrarySettings(registry) && ok;
    ok = integrations::registerIntegrationsSettings(registry) && ok;
    ok = desktop::registerPlayerSettings(registry) && ok;
    // Other modules: ok = <module>::registerSettings(registry) && ok;
    return ok;
  }

} // namespace

int main(int argc, char* argv[]) {
  // The profile folders are %APPDATA% and %LOCALAPPDATA%\Jellyfin Desktop Next
  // on Windows; the MSI removes them by these names
  // (packaging/windows/jellyfin-desktop-next.wxs), so keep them in step.
  QCoreApplication::setApplicationName(QStringLiteral("Jellyfin Desktop Next"));
  QCoreApplication::setApplicationVersion(QStringLiteral(JFD_VERSION));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("Jellyfin"));

  const Options options = earlyOptions(argc, argv);
  // The smoke test never touches the user's settings.
  std::optional<QTemporaryDir> smokeProfile;
  QString settingsFile = options.settingsFile;
  if (options.smoke && settingsFile.isEmpty()) {
    smokeProfile.emplace();
    settingsFile = smokeProfile->filePath(QStringLiteral("settings.ini"));
  }

  desktop::AppServices services;
  if (!registerSettings(services.settingsRegistry)) {
    std::fprintf(stderr, "Invalid settings schema\n");
    return 2;
  }
  services.settings = std::make_unique<settings::Settings>(
      services.settingsRegistry, settingsFile.isEmpty() ? settings::Settings::defaultFilePath() : settingsFile);

  const double scale = services.settings->value(settings::keys::Scale).toDouble();
  if (scale != 1.0 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
    qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));
  }

  QGuiApplication app(argc, argv);

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Jellyfin desktop client"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({QStringLiteral("smoke"), QStringLiteral("Open the window, visit every page, quit.")});
  parser.addOption({QStringLiteral("flow"),
                    QStringLiteral("Sign in to JFD_TEST_SERVER_URL, open a series and an episode, play them, "
                                   "switch tracks, quit.")});
  parser.addOption({QStringLiteral("settings-file"), QStringLiteral("Use this settings file."), QStringLiteral("path")});
  parser.process(app);
  if (options.flow && !desktop::FlowCheck::configured()) {
    std::fprintf(stderr, "--flow needs JFD_TEST_SERVER_URL, JFD_TEST_USER and JFD_TEST_PASSWORD\n");
    return 2;
  }

  services.isolatedProfile = smokeProfile.has_value();
  services.cacheDirectory =
      smokeProfile ? QString() : QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  services.logDirectory = smokeProfile ? smokeProfile->filePath(QStringLiteral("logs"))
                                       : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                                             QStringLiteral("/logs");
  desktop::setLogLevel(services.settings->value(settings::keys::LogLevel).toString());
  desktop::startLogFile(services.logDirectory);
  QObject::connect(services.settings.get(), &settings::Settings::valueChanged,
                   [](const QString& key, const QVariant& value) {
                     if (key == settings::keys::LogLevel) {
                       desktop::setLogLevel(value.toString());
                     }
                   });

  desktop::installTranslations(app,
                               desktop::resolveLanguage(services.settings->value(settings::keys::Language).toString()));

  // One copy per user: a second start hands its arguments over and exits.
  // The smoke test runs beside a running app.
  std::optional<platform::SingleInstance> instance;
  if (!options.smoke) {
    instance.emplace();
    if (!instance->start(app.arguments())) {
      desktop::stopLogFile();
      return 0;
    }
  }

  int exitCode = 0;
  QElapsedTimer shutdown;
  {
    QQmlApplicationEngine engine;
    desktop::installShell(services, engine);
    desktop::installLibrary(services, engine);
    desktop::installSession(services);
    desktop::installPlayer(services, engine);
    desktop::installIntegrations(services, engine);
    // The media controls and Discord follow the playback session.
    desktop::connectIntegrations(
        services, services.session->hub(),
        [session = services.session.get()](core::SessionInput input) { session->post(std::move(input)); },
        *services.client);
    desktop::installGroup(services, engine);
    // Other modules: desktop::install<Module>(services, engine);

    // Home when the saved session was restored.
    if (services.navigator->depth() == 0) {
      services.navigator->reset(QStringLiteral("login"));
    }

    engine.loadFromModule("Jfd.Desktop", "Main");
    auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window == nullptr) {
      std::fprintf(stderr, "The main window did not load\n");
      desktop::stopLogFile();
      return 3;
    }

    desktop::startEffects(services, engine, *window);

    ui::WindowStateKeeper windowState(window, *services.settings);
    windowState.restoreAndShow(QSize(1280, 800));
    desktop::startIntegrations(services, *window, !options.smoke);
    if (instance) {
      QObject::connect(&*instance, &platform::SingleInstance::activated, window, [window] {
        if (window->visibility() == QWindow::Minimized) {
          window->showNormal();
        }
        window->raise();
        window->requestActivate();
      });
    }

    std::unique_ptr<desktop::SmokeTest> smoke;
    std::unique_ptr<desktop::FlowCheck> flow;
    if (options.flow) {
      flow = std::make_unique<desktop::FlowCheck>(*window, services);
      flow->start();
    } else if (options.smoke) {
      smoke = std::make_unique<desktop::SmokeTest>(*window, *services.navigator);
      smoke->start();
    }

    exitCode = app.exec();
    shutdown.start();
    windowState.save();
    // Before the window they are attached to.
    services.integrations.reset();
    // Leave a SyncPlay group so the others are not left waiting; bounded
    // by a timeout inside prepareExit.
    bool left = false;
    QEventLoop leaving;
    desktop::prepareExit(services, [&] {
      left = true;
      leaving.quit();
    });
    if (!left) {
      leaving.exec();
    }
    if (((smoke && !smoke->passed()) || (flow && !flow->passed())) && exitCode == 0) {
      exitCode = 1;
    }
    // Leaving the scope destroys the window and the engine before the
    // services they use.
  }
  // The player screen's model and the group model subscribe to the
  // session; the session runs on the player and on the library's client
  // and socket.
  ui::PlayerSession::setInstance(nullptr);
  services.playerModel.reset();
  desktop::uninstallGroup(services);
  services.session.reset();
  services.player.reset();
  services.library.reset();
  services.settings.reset();
  desktop::stopLogFile();

  const qint64 shutdownMs = shutdown.elapsed();
  if (options.smoke) {
    std::printf("smoke: shutdown took %lld ms\n", static_cast<long long>(shutdownMs));
    if (shutdownMs >= 1000) {
      std::printf("smoke: FAILED: shutdown must take under 1 s\n");
      exitCode = 1;
    }
  }
  return exitCode;
}
