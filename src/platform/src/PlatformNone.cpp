#include <jfd/platform/MediaControls.h>
#include <jfd/platform/SleepInhibitor.h>

namespace jfd::platform {

  std::unique_ptr<SleepInhibitor> createNoSleepInhibitor(QObject* parent);
  std::unique_ptr<MediaControls> createNoMediaControls(QObject* parent);

  std::unique_ptr<SleepInhibitor> createSystemSleepInhibitor(QObject* parent) {
    return createNoSleepInhibitor(parent);
  }

  std::unique_ptr<MediaControls> createSystemMediaControls(const QString&, const QString&,
                                                           QObject* parent) {
    return createNoMediaControls(parent);
  }

} // namespace jfd::platform
