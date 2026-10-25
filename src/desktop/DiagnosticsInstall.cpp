#include "AppServices.h"

#include <jfd/api/JellyfinClient.h>
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
#include <QQuickWindow>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <deque>
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
        line += QStringLiteral("  memory %1 MB (private %2 MB)")
                    .arg(double(to->residentBytes) / 1048576.0, 0, 'f', 0)
                    .arg(double(to->privateBytes) / 1048576.0, 0, 'f', 0);
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
            showMessage(window, QCoreApplication::translate("Diagnostics", "Report saved: %1").arg(QDir::toNativeSeparators(path)),
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
  }

} // namespace jfd::desktop
