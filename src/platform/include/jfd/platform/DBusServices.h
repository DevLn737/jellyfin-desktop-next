#pragma once

#include <jfd/platform/MediaControls.h>
#include <jfd/platform/SleepInhibitor.h>

#include <QDBusConnection>

// The D-Bus implementations behind the Linux factories, on a given bus so
// tests can run them against fake services on a private bus.
namespace jfd::platform {

  std::unique_ptr<SleepInhibitor> createDBusSleepInhibitor(const QDBusConnection& bus,
                                                           QObject* parent = nullptr);

  // busSuffix: the name is org.mpris.MediaPlayer2.<busSuffix>; when it is taken
  // (another copy under a different user session bus setup), .instance<pid> is appended.
  std::unique_ptr<MediaControls> createMprisMediaControls(const QDBusConnection& bus,
                                                          const QString& busSuffix,
                                                          const QString& applicationName,
                                                          const QString& desktopEntry,
                                                          QObject* parent = nullptr);

} // namespace jfd::platform
