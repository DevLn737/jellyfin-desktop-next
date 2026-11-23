#include "AppServices.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/diagnostics/Crash.h>
#include <jfd/diagnostics/FrameStats.h>
#include <jfd/diagnostics/ProcessStats.h>
#include <jfd/diagnostics/Report.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/settings/CommonSections.h>

#include <MpvPlayer.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QLocale>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QQuickWindow>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcDiagnostics, "jfd.app.diagnostics")

    constexpr int SnapshotIntervalMs = 10'000;
    // Ten minutes of snapshots.
    constexpr std::size_t MaxSnapshots = 60;
    constexpr int ServerWaitMs = 3'000;
    constexpr int KeptReports = 5;
    // The toast that offers a report after a crash comes once the start-up
    // messages are through.
    constexpr int CrashNoticeDelayMs = 1500;
    // The toast's button opens this address, which the app handles itself.
    constexpr const char* ActionScheme = "jfd-app";
    constexpr const char* SaveReportAction = "save-report";

    std::filesystem::path toPath(const QString& path) {
#ifdef Q_OS_WIN
      return std::filesystem::path(path.toStdWString());
#else
      return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
    }

    QString fromPath(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
      return QString::fromStdWString(path.wstring());
#else
      return QFile::decodeName(QByteArray::fromStdString(path.string()));
#endif
    }

    QString crashDirectory(const AppServices& services) {
      return services.logDirectory + QStringLiteral("/crashes");
    }

    // A pause in presentation (nothing to draw) is not one long frame.
    constexpr std::int64_t IdleGapUs = 500'000;

    std::int64_t nowUs() {
      return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count();
    }

    // The frames are added on the render thread, read on the GUI thread.
    struct Statistics {
      std::mutex mutex;
      diagnostics::FrameStats frames;
      std::int64_t lastFrameUs = -1;
      std::optional<diagnostics::ProcessSample> lastProcess;
      std::deque<std::string> snapshots;
    };

    std::string std8(const QString& text) {
      return text.toStdString();
    }

    std::string formatSnapshot(const QDateTime& at, const diagnostics::FrameSummary& frames,
                               const std::optional<diagnostics::ProcessSample>& from,
                               const std::optional<diagnostics::ProcessSample>& to) {
      QString line = at.toString(QStringLiteral("HH:mm:ss"));
      if (to) {
        line += QStringLiteral("  memory %1 MB (private %2 MB %3)")
                    .arg(double(to->residentBytes) / 1048576.0, 0, 'f', 0)
                    .arg(double(to->privateBytes) / 1048576.0, 0, 'f', 0)
                    .arg(QLatin1String(diagnostics::privateBytesKind()));
        if (from) {
          line += QStringLiteral(", cpu %1% of one core").arg(diagnostics::cpuPercent(*from, *to), 0, 'f', 1);
        }
      }
      line += QStringLiteral("  frames %1").arg(frames.frames);
      if (frames.frames > 0) {
        line += QStringLiteral(", interval mean %1 ms, p95 %2 ms, max %3 ms, long %4")
                    .arg(frames.meanUs / 1000.0, 0, 'f', 1)
                    .arg(frames.p95Us / 1000.0, 0, 'f', 1)
                    .arg(frames.maxUs / 1000.0, 0, 'f', 1)
                    .arg(frames.longFrames);
      }
      return std8(line);
    }

    // The last bytes of a file, as text; the report keeps the end of a log.
    std::string readTail(const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) {
        return {};
      }
      const qint64 keep = qint64(diagnostics::MaxLogBytes) + 64 * 1024;
      if (file.size() > keep) {
        file.seek(file.size() - keep);
      }
      const QByteArray bytes = file.readAll();
      return std::string(bytes.constData(), std::size_t(bytes.size()));
    }

    QString mpvText(const media::MpvPlayer* player, const char* name) {
      return player != nullptr ? player->property(name).toString() : QString();
    }

    void showMessage(QQuickWindow& window, const QString& text, bool error) {
      QObject* shell = window.findChild<QObject*>(QStringLiteral("shell"));
      if (shell != nullptr) {
        QMetaObject::invokeMethod(shell, error ? "showError" : "showMessage", Q_ARG(QVariant, QVariant(text)));
      }
    }

    void dropOldReports(const QDir& directory) {
      const QStringList reports =
          directory.entryList({QStringLiteral("jfd-report-*.zip")}, QDir::Files, QDir::Name | QDir::Reversed);
      for (int i = KeptReports; i < reports.size(); ++i) {
        QFile::remove(directory.filePath(reports.at(i)));
      }
    }

    // Writes the report; the path, or empty after a failure.
    QString writeReport(AppServices& services, diagnostics::ReportInput input) {
      const QDateTime now = QDateTime::currentDateTimeUtc();
      input.createdAt = std8(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral(" UTC"));
      const QDir directory(services.logDirectory + QStringLiteral("/reports"));
      if (!QDir().mkpath(directory.absolutePath())) {
        return {};
      }
      const QDate date = now.date();
      const QTime time = now.time();
      const auto dosDate = std::uint16_t(((date.year() - 1980) << 9) | (date.month() << 5) | date.day());
      const auto dosTime = std::uint16_t((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
      const std::string zip = diagnostics::makeZip(diagnostics::buildReport(input), dosDate, dosTime);

      QFile file(directory.filePath(QStringLiteral("jfd-report-%1.zip").arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss")))));
      if (!file.open(QIODevice::WriteOnly) || file.write(zip.data(), qint64(zip.size())) != qint64(zip.size())) {
        return {};
      }
      file.close();
      dropOldReports(directory);
      return file.fileName();
    }
  } // namespace

  // Opens the jfd-app: addresses of the toast's buttons (QDesktopServices).
  class AppActionHandler : public QObject {
    Q_OBJECT

  public:
    explicit AppActionHandler(std::function<void(const QString&)> run, QObject* parent)
        : QObject(parent), m_run(std::move(run)) {}

  public Q_SLOTS:
    void open(const QUrl& url) { m_run(url.path()); }

  private:
    std::function<void(const QString&)> m_run;
  };

  void startCrashGuard(AppServices& services) {
    const QString pid = QString::number(QCoreApplication::applicationPid());
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-")) + pid;
    const QString info = QStringLiteral("version %1, started %2, process %3")
                             .arg(QCoreApplication::applicationVersion(),
                                  QDateTime::currentDateTime().toString(Qt::ISODate), pid);
    const auto previous = diagnostics::beginSession(
        toPath(services.logDirectory), (info + QStringLiteral("\nstamp=") + stamp).toStdString());
    const QString crashes = crashDirectory(services);
    if (previous) {
      const QStringList lines = QString::fromStdString(*previous).split(QLatin1Char('\n'));
      QString previousStamp;
      for (const QString& line : lines)
        if (line.startsWith(QLatin1String("stamp="))) previousStamp = line.mid(6);
      services.previousSessionCrashed = true;
      services.previousSessionInfo = lines.value(0);
      qCWarning(lcDiagnostics).noquote() << "the previous session ended unexpectedly:" << services.previousSessionInfo;
      // Its log, which the next start would rotate away, next to its crash
      // file (same name up to the extension).
      static const QRegularExpression safeStamp(QStringLiteral("^[0-9]{8}-[0-9]{6}-[0-9]+$"));
      if (!safeStamp.match(previousStamp).hasMatch()) previousStamp = stamp + QStringLiteral("-previous");
      QDir().mkpath(crashes);
      QFile::copy(services.logDirectory + QStringLiteral("/jellyfin-desktop.old.log"),
                  crashes + QStringLiteral("/crash-") + previousStamp + QStringLiteral(".log"));
    }
    const std::string header = "Jellyfin Desktop Next " + QCoreApplication::applicationVersion().toStdString() +
                               " crashed (" + info.toStdString() + ")";
    if (!diagnostics::installCrashHandler(toPath(crashes), stamp.toStdString(), header))
      qCWarning(lcDiagnostics) << "the crash handler could not be installed";
    diagnostics::pruneCrashFiles(toPath(crashes));
  }

  void endCrashGuard(const AppServices& services) {
    diagnostics::endSession(toPath(services.logDirectory));
  }

  void installDiagnostics(AppServices& services, QQuickWindow& window) {
    auto statistics = std::make_shared<Statistics>();

    // The render thread: one timestamp per presented frame.
    QObject::connect(
        &window, &QQuickWindow::frameSwapped, &window,
        [statistics] {
          const std::int64_t now = nowUs();
          std::lock_guard lock(statistics->mutex);
          if (statistics->lastFrameUs >= 0 && now - statistics->lastFrameUs > IdleGapUs) {
            statistics->frames.breakSequence();
          }
          statistics->lastFrameUs = now;
          statistics->frames.addFrame(now);
        },
        Qt::DirectConnection);

    auto* timer = new QTimer(&window);
    timer->setInterval(SnapshotIntervalMs);
    QObject::connect(timer, &QTimer::timeout, &window, [statistics, &window] {
      const auto process = diagnostics::sampleCurrentProcess();
      const double refreshUs = window.screen() != nullptr && window.screen()->refreshRate() > 0
                                   ? 1e6 / window.screen()->refreshRate()
                                   : 0.0;
      std::lock_guard lock(statistics->mutex);
      const diagnostics::FrameSummary summary = statistics->frames.summary(refreshUs);
      statistics->frames.clearIntervals();
      statistics->snapshots.push_back(
          formatSnapshot(QDateTime::currentDateTime(), summary, statistics->lastProcess, process));
      while (statistics->snapshots.size() > MaxSnapshots) {
        statistics->snapshots.pop_front();
      }
      statistics->lastProcess = process;
    });
    timer->start();

    QObject::connect(
        services.settings.get(), &settings::Settings::actionTriggered, &window,
        [&services, &window, statistics](const QString& key) {
          if (key != settings::keys::SaveReport) {
            return;
          }
          diagnostics::ReportInput input;
          input.clientVersion = std8(QCoreApplication::applicationVersion());
          input.qtVersion = std8(QStringLiteral("%1 (built with %2)").arg(QLatin1String(qVersion()), QLatin1String(QT_VERSION_STR)));
          input.osName = std8(QSysInfo::prettyProductName());
          input.cpuArchitecture = std8(QSysInfo::currentCpuArchitecture());
          input.language = std8(QLocale().name());
          input.gpu = std8(services.graphicsDescription);
          if (services.player) {
            input.mpvVersion = std8(mpvText(services.player.get(), "mpv-version"));
            input.ffmpegVersion = std8(mpvText(services.player.get(), "ffmpeg-version"));
            input.hwdecCurrent = std8(services.player->hwdecCurrent());
          }
          input.hwdecSetting = std8(services.settings->value(QStringLiteral("playback/hardwareDecoding")).toString());
          for (const settings::SectionDescriptor& section : services.settingsRegistry.sections()) {
            for (const settings::SettingDescriptor& setting : section.settings) {
              const QString full = section.id + QLatin1Char('/') + setting.key;
              // Not choices: state, an action, or a folder on this computer.
              if (setting.type == settings::SettingType::Action || setting.hidden || section.hidden ||
                  full.startsWith(QLatin1String("legacy/"))) {
                continue;
              }
              input.settings.emplace_back(std8(full), std8(services.settings->value(full).toString()));
            }
          }
          input.log = readTail(services.logDirectory + QStringLiteral("/jellyfin-desktop.log"));
          input.previousLog = readTail(services.logDirectory + QStringLiteral("/jellyfin-desktop.old.log"));
          if (services.session) {
            input.sessionJournal = services.session->journal().lines();
          }
          input.previousSessionCrashed = services.previousSessionCrashed;
          input.previousSessionInfo = std8(services.previousSessionInfo);
          const std::vector<std::filesystem::path> crashes = diagnostics::crashFiles(toPath(crashDirectory(services)));
          for (const std::filesystem::path& file : crashes) {
            const QFileInfo crash(fromPath(file));
            input.crashFiles.push_back(
                std8(QStringLiteral("%1 (%2 KB)").arg(crash.fileName()).arg((crash.size() + 1023) / 1024)));
          }
          // The newest crash's backtrace (Linux); a minidump stays in the
          // folder, for whoever asks for it.
          if (!crashes.empty()) {
            const QString newest = fromPath(crashes.front());
            const QString text = newest.left(newest.size() - QFileInfo(newest).suffix().size()) + QStringLiteral("txt");
            input.crashText = readTail(text);
          }
          {
            std::lock_guard lock(statistics->mutex);
            input.statsSnapshots.assign(statistics->snapshots.begin(), statistics->snapshots.end());
          }
          if (const auto sample = diagnostics::sampleCurrentProcess()) {
            input.statsSnapshots.push_back(
                formatSnapshot(QDateTime::currentDateTime(), {}, std::nullopt, sample) + "  (when the report was made)");
          }

          // The server's version, when one is set; the report waits a few
          // seconds for it at most.
          auto finish = std::make_shared<std::function<void(const QString&)>>();
          auto done = std::make_shared<bool>(false);
          *finish = [&services, &window, input = std::move(input), done](const QString& serverVersion) mutable {
            if (*done) {
              return;
            }
            *done = true;
            input.serverVersion = std8(serverVersion);
            const QString path = writeReport(services, std::move(input));
            if (path.isEmpty()) {
              qCWarning(lcDiagnostics) << "cannot write the diagnostic report";
              showMessage(window, QCoreApplication::translate("Diagnostics", "Could not save the report."), true);
              return;
            }
            qCInfo(lcDiagnostics).noquote() << "report saved:" << path;
            showMessage(window, QCoreApplication::translate("Diagnostics", "Report saved: %1").arg(QFileInfo(path).fileName()),
                        false);
            if (!services.isolatedProfile) {
              QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
            }
          };
          if (services.client == nullptr || services.client->serverUrl().isEmpty()) {
            (*finish)(QString());
            return;
          }
          services.client->publicServerInfo(&window, [finish](api::Result<api::PublicServerInfo> info) {
            (*finish)(info ? info->versionText : QString());
          });
          QTimer::singleShot(ServerWaitMs, &window, [finish] { (*finish)(QString()); });
        });

    // After a crash: say so, with a button that saves the report.
    auto* actions = new AppActionHandler(
        [&services](const QString& action) {
          if (action == QLatin1String(SaveReportAction)) services.settings->trigger(settings::keys::SaveReport);
        },
        &window);
    QDesktopServices::setUrlHandler(QLatin1String(ActionScheme), actions, "open");
    QObject::connect(actions, &QObject::destroyed, [] { QDesktopServices::unsetUrlHandler(QLatin1String(ActionScheme)); });
    if (services.previousSessionCrashed) {
      QTimer::singleShot(CrashNoticeDelayMs, &window, [&window] {
        QObject* shell = window.findChild<QObject*>(QStringLiteral("shell"));
        if (shell == nullptr) return;
        QMetaObject::invokeMethod(
            shell, "showLink",
            Q_ARG(QVariant, QVariant(QCoreApplication::translate(
                                "Diagnostics",
                                "The previous session ended unexpectedly. A diagnostic report helps to find out why."))),
            Q_ARG(QVariant, QVariant(QCoreApplication::translate("Diagnostics", "Save report"))),
            Q_ARG(QVariant, QVariant(QStringLiteral("%1:%2").arg(QLatin1String(ActionScheme),
                                                                  QLatin1String(SaveReportAction)))));
      });
    }
  }

} // namespace jfd::desktop

#include "DiagnosticsInstall.moc"
