#pragma once

#include <QString>

namespace jfd::desktop {

  // Writes Qt log output to <directory>/jellyfin-desktop.log (the previous
  // run's log is kept as jellyfin-desktop.old.log) and to stderr, with
  // server access keys and passwords replaced by *** (diagnostics::redactSecrets).
  // The file is capped at 10 MiB: past that it becomes the old log and a
  // new one starts. Warnings and errors are flushed at once, other lines
  // within a second (diagnostics::LogFile).
  void startLogFile(const QString& directory);
  // Writes the buffered lines to disk now (before a report or an exit).
  void flushLogFile();
  // Keeps what is logged from now until startLogFile() and writes it to the
  // file first (the 2.x import runs before the log directory is known).
  // Debug lines are not kept.
  void bufferEarlyLog();
  void stopLogFile();

  // "error", "warning", "info" or "debug" (diagnostics/logLevel).
  void setLogLevel(const QString& level);

} // namespace jfd::desktop
