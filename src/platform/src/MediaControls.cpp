#include <jfd/platform/MediaControls.h>

namespace jfd::platform {

  namespace {
    class NoMediaControls final : public MediaControls {
    public:
      using MediaControls::MediaControls;
      bool isAvailable() const override { return false; }
      void attachWindow(QWindow*) override {}
      void setState(const MediaControlsState&) override {}
    };
  } // namespace

  std::unique_ptr<MediaControls> createNoMediaControls(QObject* parent) {
    return std::make_unique<NoMediaControls>(parent);
  }

} // namespace jfd::platform
