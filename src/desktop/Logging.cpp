#include "Logging.h"

#include <jfd/diagnostics/Redact.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>

#include <cstdio>
#include <mutex>

namespace jfd::desktop {

  namespace {
    std::mutex logMutex;
    QFile* logFile = nullptr;
    QtMessageHandler previousHandler = nullptr;

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
        }
      }
      if (previousHandler != nullptr) {
        previousHandler(type, context, message);
      }
    }
  } // namespace

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
      logFile = file;
    }
    previousHandler = qInstallMessageHandler(writeLine);
  }

  void stopLogFile() {
    qInstallMessageHandler(previousHandler);
    std::lock_guard lock(logMutex);
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
