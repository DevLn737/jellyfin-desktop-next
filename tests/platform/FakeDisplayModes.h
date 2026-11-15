#pragma once

#include <jfd/platform/DisplayModes.h>

#include <QHash>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <utility>

namespace jfd::test {

  // Display modes without a display: screens with their mode lists, a
  // current mode per screen, and a record of every switch. A switch takes
  // effect after `switchDelayMs` (the system's notice comes with it), or
  // never when `confirms` is false (a display that does not answer).
  class FakeDisplayModes final : public platform::DisplayModes {
  public:
    using DisplayModes::DisplayModes;

    struct Switch {
      QString screen;
      platform::DisplayMode mode;
    };

    bool supported = true;
    bool refuses = false;
    bool confirms = true;
    int switchDelayMs = 0;
    QHash<QString, QList<platform::DisplayMode>> lists;
    QHash<QString, platform::DisplayMode> current;
    QList<Switch> switches;
    // Runs as each switch is asked for, before it is recorded.
    std::function<void(const QString&, const platform::DisplayMode&)> beforeSwitch;

    static platform::DisplayMode mode(double rate, int width = 1920, int height = 1080) {
      platform::DisplayMode m;
      m.width = width;
      m.height = height;
      m.refreshRate = rate;
      return m;
    }

    // A screen at `currentRate`, listing these rates at 1920x1080.
    void addScreen(const QString& name, double currentRate, const QList<double>& rates) {
      QList<platform::DisplayMode> modes;
      for (double rate : rates) modes.append(mode(rate));
      lists.insert(name, modes);
      current.insert(name, mode(currentRate));
    }

    double rate(const QString& screen) const { return current.value(screen).refreshRate; }

    QString backend() const override { return QStringLiteral("fake"); }
    bool isSupported() const override { return supported; }
    QStringList screens() const override { return supported ? lists.keys() : QStringList(); }
    QList<platform::DisplayMode> modes(const QString& screen) const override {
      return supported ? lists.value(screen) : QList<platform::DisplayMode>();
    }
    std::optional<platform::DisplayMode> currentMode(const QString& screen) const override {
      if (!supported || !current.contains(screen)) return std::nullopt;
      return current.value(screen);
    }
    bool setMode(const QString& screen, const platform::DisplayMode& mode) override {
      if (!supported || refuses || !current.contains(screen)) return false;
      if (beforeSwitch) beforeSwitch(screen, mode);
      switches.append({screen, mode});
      if (!confirms) return true;
      QTimer::singleShot(switchDelayMs, this, [this, screen, mode] {
        current.insert(screen, mode);
        Q_EMIT modeChanged();
      });
      return true;
    }
  };

} // namespace jfd::test
