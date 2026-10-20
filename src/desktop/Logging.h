#pragma once

#include <QString>

namespace jfd::desktop {

  // Writes Qt log output to <directory>/jellyfin-desktop.log (the previous
  // run's log is kept as jellyfin-desktop.old.log) and to stderr.
  void startLogFile(const QString& directory);
  void stopLogFile();

  // "error", "warning", "info" or "debug" (diagnostics/logLevel).
  void setLogLevel(const QString& level);

} // namespace jfd::desktop
