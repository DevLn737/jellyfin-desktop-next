#include "AppServices.h"
#include "IntegrationServices.h"
#include "Logging.h"
#include "SmokeTest.h"
#include "Translations.h"

#include <jfd/integrations/IntegrationsSettings.h>
#include <jfd/settings/CommonSections.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/WindowStateKeeper.h>

#include <QCommandLineParser>
#include <QElapsedTimer>
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

using namespace jfd;

namespace {

  struct Options {
    bool smoke = false;
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
    ok = integrations::registerIntegrationsSettings(registry) && ok;
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
  parser.addOption({QStringLiteral("settings-file"), QStringLiteral("Use this settings file."), QStringLiteral("path")});
  parser.process(app);

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

  // Single instance: joins here once the platform module provides it.

  int exitCode = 0;
  QElapsedTimer shutdown;
  {
    QQmlApplicationEngine engine;
    desktop::installShell(services, engine);
    desktop::installIntegrations(services, engine);
    // Other modules: desktop::install<Module>(services, engine);

    services.navigator->reset(QStringLiteral("login"));

    engine.loadFromModule("Jfd.Desktop", "Main");
    auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window == nullptr) {
      std::fprintf(stderr, "The main window did not load\n");
      desktop::stopLogFile();
      return 3;
    }

    ui::WindowStateKeeper windowState(window, *services.settings);
    windowState.restoreAndShow(QSize(1280, 800));
    desktop::startIntegrations(services, *window, !options.smoke);

    std::unique_ptr<desktop::SmokeTest> smoke;
    if (options.smoke) {
      smoke = std::make_unique<desktop::SmokeTest>(*window, *services.navigator);
      smoke->start();
    }

    exitCode = app.exec();
    shutdown.start();
    windowState.save();
    // Before the window they are attached to.
    services.integrations.reset();
    if (smoke && !smoke->passed() && exitCode == 0) {
      exitCode = 1;
    }
    // Leaving the scope destroys the window and the engine before the
    // services they use.
  }
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
