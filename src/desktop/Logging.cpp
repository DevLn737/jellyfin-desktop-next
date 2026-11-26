#include "Logging.h"

#include <jfd/diagnostics/LogFile.h>
#include <jfd/diagnostics/Redact.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QList>
#include <QLoggingCategory>
#include <QTimer>

#include <cstdio>
#include <filesystem>
#include <string_view>
#include <mutex>

namespace jfd::desktop {

  namespace {
    std::mutex logMutex;
    // At most 10 MiB, then moved aside to the old file (one is kept);
    // flushed at once for warnings and errors, within a second otherwise.
    diagnostics::LogFile* logFile = nullptr;
    // Flushes the quiet lines that no later line takes along.
    QTimer* flushTimer = nullptr;
    QtMessageHandler previousHandler = nullptr;
    bool installed = false;
    // Lines logged before the log file starts (bufferEarlyLog()), written
    // to it first.
    bool buffering = false;
    QList<QByteArray> earlyLines;
    constexpr qsizetype MaxEarlyLines = 500;

    // The path as the standard library takes it, without a round trip
    // through the C locale.
    std::filesystem::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
      return std::filesystem::path(QDir::toNativeSeparators(path).toStdWString());
#else
      return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
    }

    const char* levelName(QtMsgType type) {
      switch (type) {
      case QtDebugMsg:
        return "debug";
      case QtInfoMsg:
        return "info";
      case QtWarningMsg:
        return "warning";
      case QtCriticalMsg:
        return "critical";
      case QtFatalMsg:
        return "fatal";
      }
      return "?";
    }

    void writeLine(QtMsgType type, const QMessageLogContext& context, const QString& raw) {
      // Stream and socket addresses carry the server access key, and mpv
      // prints them whole when a file does not open.
      const QByteArray utf8 = raw.toUtf8();
      const std::string redacted = diagnostics::redactSecrets(std::string_view(utf8.constData(), utf8.size()));
      const QString message = QString::fromStdString(redacted);
      const QByteArray line =
          QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toUtf8() + ' ' +
          levelName(type) + " [" + QByteArray(context.category != nullptr ? context.category : "default") +
          "] " + QByteArray::fromStdString(redacted) + '\n';
      {
        std::lock_guard lock(logMutex);
        if (logFile != nullptr) {
          // Warnings, errors and fatal messages reach the disk at once, with
          // every line before them: a crash right after loses no more than
          // a second of debug and info lines.
          logFile->write(std::string_view(line.constData(), static_cast<size_t>(line.size())), type != QtDebugMsg && type != QtInfoMsg);
        } else if (buffering && type != QtDebugMsg && earlyLines.size() < MaxEarlyLines) {
          earlyLines.append(line);
        }
      }
      if (previousHandler != nullptr) {
        previousHandler(type, context, message);
      }
    }
  } // namespace

  void bufferEarlyLog() {
    {
      std::lock_guard lock(logMutex);
      buffering = true;
    }
    if (!installed) {
      previousHandler = qInstallMessageHandler(writeLine);
      installed = true;
    }
  }

  void startLogFile(const QString& directory) {
    QDir().mkpath(directory);
    const QString path = directory + QStringLiteral("/jellyfin-desktop.log");
    const QString old = directory + QStringLiteral("/jellyfin-desktop.old.log");
    auto* file = new diagnostics::LogFile;
    if (!file->open(nativePath(path), nativePath(old))) {
      std::fprintf(stderr, "Cannot write the log file %s\n", qPrintable(path));
      delete file;
      return;
    }
    {
      std::lock_guard lock(logMutex);
      for (const QByteArray& line : earlyLines)
        file->write(std::string_view(line.constData(), static_cast<size_t>(line.size())), false);
      file->flush();
      earlyLines.clear();
      buffering = false;
      logFile = file;
    }
    if (flushTimer == nullptr) {
      flushTimer = new QTimer;
      flushTimer->setInterval(1000);
      QObject::connect(flushTimer, &QTimer::timeout, [] {
        std::lock_guard lock(logMutex);
        if (logFile != nullptr) logFile->flushIfDue();
      });
      flushTimer->start();
    }
    if (!installed) {
      previousHandler = qInstallMessageHandler(writeLine);
      installed = true;
    }
  }

  void flushLogFile() {
    std::lock_guard lock(logMutex);
    if (logFile != nullptr) logFile->flush();
  }

  void stopLogFile() {
    if (installed) {
      qInstallMessageHandler(previousHandler);
      installed = false;
    }
    delete flushTimer;
    flushTimer = nullptr;
    std::lock_guard lock(logMutex);
    buffering = false;
    earlyLines.clear();
    delete logFile;
    logFile = nullptr;
  }

  void setLogLevel(const QString& level) {
    // Warnings and errors are always logged; the level adds the rest.
    QString rules = QStringLiteral("*.debug=false\n*.info=false\n");
    if (level == QLatin1String("debug")) {
      rules = QStringLiteral("*.debug=true\n*.info=true\nqt.*.debug=false\n");
    } else if (level == QLatin1String("info")) {
      rules = QStringLiteral("*.debug=false\n*.info=true\nqt.*.info=false\n");
    } else if (level == QLatin1String("error")) {
      rules += QStringLiteral("*.warning=false\n");
    }
    QLoggingCategory::setFilterRules(rules);
  }

} // namespace jfd::desktop
