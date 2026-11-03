#include "Logging.h"

#include <jfd/diagnostics/Redact.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QList>
#include <QLoggingCategory>

#include <cstdio>
#include <mutex>

namespace jfd::desktop {

  namespace {
    std::mutex logMutex;
    QFile* logFile = nullptr;
    QtMessageHandler previousHandler = nullptr;
    bool installed = false;
    // Lines logged before the log file starts (bufferEarlyLog()), written
    // to it first.
    bool buffering = false;
    QList<QByteArray> earlyLines;
    constexpr qsizetype MaxEarlyLines = 500;

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
          logFile->write(line);
          logFile->flush();
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
    QFile::remove(old);
    QFile::rename(path, old);
    auto* file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Text)) {
      std::fprintf(stderr, "Cannot write the log file %s\n", qPrintable(path));
      delete file;
      return;
    }
    {
      std::lock_guard lock(logMutex);
      for (const QByteArray& line : earlyLines) file->write(line);
      file->flush();
      earlyLines.clear();
      buffering = false;
      logFile = file;
    }
    if (!installed) {
      previousHandler = qInstallMessageHandler(writeLine);
      installed = true;
    }
  }

  void stopLogFile() {
    if (installed) {
      qInstallMessageHandler(previousHandler);
      installed = false;
    }
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
