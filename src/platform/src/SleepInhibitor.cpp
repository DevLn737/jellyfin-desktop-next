#include <jfd/platform/SleepInhibitor.h>

namespace jfd::platform {

  void SleepInhibitor::setActive(bool active, Backend backend) {
    const bool changed = active != m_active;
    m_active = active;
    m_backend = active ? backend : Backend::None;
    if (changed) Q_EMIT activeChanged(active);
  }

  namespace {
    class NoSleepInhibitor final : public SleepInhibitor {
    public:
      using SleepInhibitor::SleepInhibitor;
      void setInhibited(bool, const QString&) override {}
    };
  } // namespace

  std::unique_ptr<SleepInhibitor> createNoSleepInhibitor(QObject* parent) {
    return std::make_unique<NoSleepInhibitor>(parent);
  }

} // namespace jfd::platform
