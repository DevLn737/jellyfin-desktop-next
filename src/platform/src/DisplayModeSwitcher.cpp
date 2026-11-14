#include <jfd/platform/DisplayModeSwitcher.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace jfd::platform {

  namespace {
    Q_LOGGING_CATEGORY(lcSwitch, "jfd.platform.display")

    constexpr int JournalVersion = 1;

    QJsonObject toJson(const QString& backend, const QString& screen, const DisplayMode& mode) {
      return {{QStringLiteral("version"), JournalVersion},
              {QStringLiteral("backend"), backend},
              {QStringLiteral("screen"), screen},
              {QStringLiteral("width"), mode.width},
              {QStringLiteral("height"), mode.height},
              {QStringLiteral("refreshRate"), mode.refreshRate},
              {QStringLiteral("interlaced"), mode.interlaced},
              // As a string: JSON numbers lose 64-bit ids.
              {QStringLiteral("id"), QString::number(mode.id)}};
    }

    DisplayMode modeFromJson(const QJsonObject& object) {
      DisplayMode mode;
      mode.width = object.value(QStringLiteral("width")).toInt();
      mode.height = object.value(QStringLiteral("height")).toInt();
      mode.refreshRate = object.value(QStringLiteral("refreshRate")).toDouble();
      mode.interlaced = object.value(QStringLiteral("interlaced")).toBool();
      mode.id = object.value(QStringLiteral("id")).toString().toULongLong();
      return mode;
    }

    // The screen's own listing of `wanted` (an XRandR id may differ from
    // the one saved by another X session); the saved mode when none.
    DisplayMode listed(const DisplayModes& modes, const QString& screen, const DisplayMode& wanted) {
      const QList<DisplayMode> all = modes.modes(screen);
      for (const DisplayMode& mode : all)
        if (mode == wanted && mode.id == wanted.id) return mode;
      for (const DisplayMode& mode : all)
        if (mode == wanted) return mode;
      return wanted;
    }
  } // namespace

  DisplayModeSwitcher::DisplayModeSwitcher(DisplayModes& modes, QString journalPath, QObject* parent)
      : QObject(parent), m_modes(modes), m_journalPath(std::move(journalPath)) {
    m_settle.setSingleShot(true);
    connect(&m_settle, &QTimer::timeout, this, [this] {
      qCInfo(lcSwitch) << "display mode not confirmed within" << m_settleMs << "ms; opening anyway";
      finishWaiting();
    });
    connect(&m_modes, &DisplayModes::modeChanged, this, &DisplayModeSwitcher::onModeChanged);
  }

  DisplayModeSwitcher::~DisplayModeSwitcher() {
    m_ready = nullptr;
    restore();
  }

  void DisplayModeSwitcher::setSettleMs(int ms) { m_settleMs = std::clamp(ms, 0, MaxSettleMs); }

  ModeChoice DisplayModeSwitcher::prepare(const QString& screen, double frameRate, std::function<void()> ready) {
    cancel();
    const auto proceed = [&ready] {
      if (ready) ready();
    };
    if (!m_modes.isSupported()) {
      proceed();
      return {};
    }
    if (m_original && m_screen != screen) restore();

    const std::optional<DisplayMode> current = m_modes.currentMode(screen);
    if (!current) {
      qCInfo(lcSwitch) << "no current mode for screen" << screen;
      proceed();
      return {};
    }
    // Already switched for the video before: choose as from the original
    // mode, so a video the original fits goes back to it.
    const DisplayMode base = m_original.value_or(*current);
    const ModeChoice choice = chooseDisplayMode(frameRate, base, m_modes.modes(screen));
    const DisplayMode target = choice.kind == ModeChoice::Kind::Switch ? choice.mode : base;
    if (choice.kind == ModeChoice::Kind::NoMatch)
      qCInfo(lcSwitch) << "no mode of" << screen << "fits a video at" << frameRate << "fps";
    if (*current == target) {
      qCInfo(lcSwitch) << "video at" << frameRate << "fps plays at" << current->toString() << "on" << screen;
      proceed();
      return choice;
    }

    const bool first = !m_original;
    if (first) {
      // Before anything changes: what to go back to, even after a crash.
      if (!writeJournal(screen, *current)) {
        qCWarning(lcSwitch) << "could not write the display mode journal" << m_journalPath
                            << "; the mode is not changed";
        proceed();
        return {};
      }
      m_original = *current;
      m_screen = screen;
    }
    const bool back = *m_original == target;
    qCInfo(lcSwitch) << "switching" << screen << "from" << current->toString() << "to" << target.toString()
                     << "for a video at" << frameRate << "fps";
    if (!m_modes.setMode(screen, listed(m_modes, screen, target))) {
      qCWarning(lcSwitch) << "the system refused" << target.toString() << "on" << screen;
      if (first) {
        m_original.reset();
        m_screen.clear();
        removeJournal();
      }
      proceed();
      return {};
    }
    if (back) {
      // Back at the original: nothing left to restore.
      m_original.reset();
      m_screen.clear();
      removeJournal();
    }
    if (first || back) Q_EMIT switchedChanged();
    m_target = target;
    m_waitScreen = screen;
    m_ready = ready ? std::move(ready) : std::function<void()>([] {});
    m_settle.start(m_settleMs);
    return choice;
  }

  void DisplayModeSwitcher::onModeChanged() {
    if (!m_ready || !m_target) return;
    const std::optional<DisplayMode> now = m_modes.currentMode(m_waitScreen);
    if (now && *now == *m_target) {
      qCInfo(lcSwitch) << "display mode confirmed:" << now->toString();
      finishWaiting();
    }
  }

  void DisplayModeSwitcher::finishWaiting() {
    m_settle.stop();
    m_target.reset();
    if (auto ready = std::exchange(m_ready, nullptr)) ready();
  }

  void DisplayModeSwitcher::cancel() {
    m_settle.stop();
    m_target.reset();
    m_ready = nullptr;
  }

  void DisplayModeSwitcher::restore() {
    // A switch not confirmed yet may still land: put the original back
    // whatever the system reports now.
    const bool unconfirmed = m_target.has_value();
    cancel();
    if (!m_original) return;
    const DisplayMode original = *std::exchange(m_original, std::nullopt);
    const QString screen = std::exchange(m_screen, QString());
    const std::optional<DisplayMode> current = m_modes.currentMode(screen);
    if (unconfirmed || !current || !(*current == original)) {
      qCInfo(lcSwitch) << "restoring" << screen << "to" << original.toString();
      if (!m_modes.setMode(screen, listed(m_modes, screen, original)))
        qCWarning(lcSwitch) << "could not restore" << screen << "to" << original.toString();
    }
    removeJournal();
    Q_EMIT switchedChanged();
  }

  bool DisplayModeSwitcher::restorePending() {
    if (m_journalPath.isEmpty() || !QFile::exists(m_journalPath)) return false;
    QFile file(m_journalPath);
    QJsonObject journal;
    if (file.open(QIODevice::ReadOnly)) journal = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    bool restored = false;
    const QString screen = journal.value(QStringLiteral("screen")).toString();
    const DisplayMode original = modeFromJson(journal);
    if (journal.value(QStringLiteral("version")).toInt() != JournalVersion ||
        !journal.value(QStringLiteral("screen")).isString() || original.refreshRate <= 0) {
      qCWarning(lcSwitch) << "unreadable display mode journal" << m_journalPath << "removed";
    } else if (journal.value(QStringLiteral("backend")).toString() != m_modes.backend() || !m_modes.isSupported()) {
      qCInfo(lcSwitch) << "display mode journal from another session type; not applied";
    } else {
      const std::optional<DisplayMode> current = m_modes.currentMode(screen);
      if (!current) {
        qCInfo(lcSwitch) << "screen" << screen << "from the display mode journal is not connected";
      } else if (*current == original) {
        qCInfo(lcSwitch) << screen << "is already back at" << original.toString();
      } else if (m_modes.setMode(screen, listed(m_modes, screen, original))) {
        qCInfo(lcSwitch) << "restored" << screen << "to" << original.toString() << "after an unfinished run";
        restored = true;
      } else {
        qCWarning(lcSwitch) << "could not restore" << screen << "to" << original.toString();
      }
    }
    removeJournal();
    return restored;
  }

  bool DisplayModeSwitcher::writeJournal(const QString& screen, const DisplayMode& mode) const {
    if (m_journalPath.isEmpty()) return true;
    QDir().mkpath(QFileInfo(m_journalPath).absolutePath());
    QSaveFile file(m_journalPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(toJson(m_modes.backend(), screen, mode)).toJson(QJsonDocument::Compact));
    return file.commit();
  }

  void DisplayModeSwitcher::removeJournal() const {
    if (!m_journalPath.isEmpty()) QFile::remove(m_journalPath);
  }

} // namespace jfd::platform
