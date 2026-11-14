#pragma once

#include <jfd/platform/DisplayModes.h>

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

// Switches a screen to the mode chooseDisplayMode picks for a video, waits
// for the display to settle, and puts the original mode back.
//
// The original mode is written to a journal file in the profile before the
// first switch and removed once it is back. A run that crashed (or was
// killed) while switched leaves the file; restorePending() at the next start
// puts that mode back. SetDisplayConfig on Windows and XRandR are not undone
// by the system when the process ends.
namespace jfd::platform {

  class DisplayModeSwitcher : public QObject {
    Q_OBJECT
  public:
    static constexpr int DefaultSettleMs = 2000;
    static constexpr int MaxSettleMs = 5000;

    // `journalPath`: a file in the profile ("display-mode.json"); empty
    // keeps no journal (crash recovery off).
    DisplayModeSwitcher(DisplayModes& modes, QString journalPath, QObject* parent = nullptr);
    ~DisplayModeSwitcher() override;

    DisplayModes& modes() const { return m_modes; }
    QString journalPath() const { return m_journalPath; }

    // The longest wait for the display after a switch; 0 to 5000 ms.
    void setSettleMs(int ms);
    int settleMs() const { return m_settleMs; }

    // Chooses the mode for a video at `frameRate` on `screen` and switches
    // to it. While already switched (the next episode), the choice starts
    // from the original mode, and a video the original fits goes back to
    // it. `ready` runs once the system reports the new mode or after
    // settleMs, whichever comes first; at once (inside this call) when
    // nothing changes, the backend is not supported or the switch fails.
    // A call while switched to another screen puts that screen back first.
    // A `ready` still waiting from an earlier call is dropped.
    ModeChoice prepare(const QString& screen, double frameRate, std::function<void()> ready);
    // Drops a `ready` still waiting, without running it. The mode stays.
    void cancel();
    // Puts the original mode back (if switched) and removes the journal.
    // Drops a `ready` still waiting.
    void restore();
    // At start: a journal left by a run that did not restore is applied and
    // removed. True when a mode was put back.
    bool restorePending();

    bool isSwitched() const { return m_original.has_value(); }
    bool isWaiting() const { return static_cast<bool>(m_ready); }
    // The screen switched now; empty when not switched.
    QString switchedScreen() const { return m_screen; }

  Q_SIGNALS:
    void switchedChanged();

  private:
    void finishWaiting();
    void onModeChanged();
    bool writeJournal(const QString& screen, const DisplayMode& mode) const;
    void removeJournal() const;

    DisplayModes& m_modes;
    QString m_journalPath;
    int m_settleMs = DefaultSettleMs;
    QTimer m_settle;
    std::function<void()> m_ready;
    std::optional<DisplayMode> m_target;
    QString m_waitScreen;
    // Before the first switch, while switched.
    std::optional<DisplayMode> m_original;
    QString m_screen;
  };

} // namespace jfd::platform
