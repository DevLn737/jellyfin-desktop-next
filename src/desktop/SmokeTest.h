#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class QQuickWindow;

namespace jfd::ui {
  class Navigator;
}

namespace jfd::desktop {

  // --smoke: checks that the window shows the login page, walks every route
  // and back, then quits. Prints each step; the exit code is 0 on success.
  class SmokeTest : public QObject {
    Q_OBJECT
  public:
    SmokeTest(QQuickWindow& window, ui::Navigator& navigator, QObject* parent = nullptr);

    void start();
    bool passed() const { return m_passed; }

  private:
    void step();
    void fail(const QString& reason);
    QString shownRoute() const;

    QQuickWindow& m_window;
    ui::Navigator& m_navigator;
    QStringList m_routes;
    QString m_expected;
    int m_next = 0;
    bool m_backPhase = false;
    bool m_passed = false;
    QTimer m_timeout;
  };

} // namespace jfd::desktop
