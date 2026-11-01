#include <jfd/platform/SleepInhibitor.h>

#include <windows.h>

namespace jfd::platform {

  namespace {
    // SetThreadExecutionState applies to the calling thread; the session calls
    // this from the GUI thread, which lives as long as the application.
    class WindowsSleepInhibitor final : public SleepInhibitor {
    public:
      using SleepInhibitor::SleepInhibitor;
      ~WindowsSleepInhibitor() override {
        if (isInhibited()) SetThreadExecutionState(ES_CONTINUOUS);
      }

      void setInhibited(bool inhibited, const QString&, Level level) override {
        if (inhibited == isInhibited() && (!inhibited || level == m_level)) return;
        m_level = level;
        const EXECUTION_STATE display = level == Level::Display ? ES_DISPLAY_REQUIRED : 0;
        const EXECUTION_STATE state = inhibited ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED | display : ES_CONTINUOUS;
        if (SetThreadExecutionState(state) == 0) {
          qWarning("SetThreadExecutionState failed: %lu", GetLastError());
          return;
        }
        setActive(inhibited, Backend::WindowsExecutionState);
      }

    private:
      Level m_level = Level::Display;
    };
  } // namespace

  std::unique_ptr<SleepInhibitor> createSystemSleepInhibitor(QObject* parent) {
    return std::make_unique<WindowsSleepInhibitor>(parent);
  }

} // namespace jfd::platform
