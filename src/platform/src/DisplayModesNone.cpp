#include <jfd/platform/DisplayModes.h>

// Systems without a backend (and Linux builds without Xlib and XRandR).
namespace jfd::platform {

  bool displayModesAvailable() { return false; }

  std::unique_ptr<DisplayModes> createSystemDisplayModes(QObject* parent) {
    return createUnsupportedDisplayModes(parent);
  }

} // namespace jfd::platform
