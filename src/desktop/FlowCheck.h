#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class QQuickWindow;

namespace jfd::desktop {

  struct AppServices;

  // --flow, with JFD_TEST_SERVER_URL, JFD_TEST_USER and JFD_TEST_PASSWORD
  // from scripts/test-server/stock-server.sh: the checkpoint-1 path on a
  // real server. Signs in on the login page, opens home, the series, an
  // episode, plays it, then plays the movie and switches its audio and
  // subtitle tracks (embedded and external), stops and quits. Prints each
  // step with its time; JFD_SMOKE_SCREENSHOTS saves a picture of each. The
  // exit code is 0 on success.
  class FlowCheck : public QObject {
    Q_OBJECT
  public:
    FlowCheck(QQuickWindow& window, AppServices& services, QObject* parent = nullptr);

    static bool configured();
    void start();
    bool passed() const { return m_passed; }

  private:
    using Check = std::function<bool()>;
    using Next = std::function<void()>;

    // Polls check until it holds, then runs next; fails after timeoutMs.
    void waitFor(const QString& what, Check check, int timeoutMs, Next next);
    void done(const QString& what);
    void fail(const QString& reason);
    void screenshot(const QString& name);
    QString shownRoute() const;
    bool shows(const QString& route) const;

    void signIn();
    void openSeries();
    void openEpisode(const QString& seriesId);
    void playEpisode(const QString& episodeId);
    void playMovie();
    void switchAudio();
    void switchSubtitles(bool external);
    void finish();

    QQuickWindow& m_window;
    AppServices& m_services;
    QTimer m_poll;
    QElapsedTimer m_stepClock;
    QElapsedTimer m_waitClock;
    QString m_waiting;
    Check m_check;
    Next m_next;
    int m_timeoutMs = 0;
    double m_positionBefore = 0;
    int m_wantedIndex = -1;
    bool m_passed = false;
    bool m_failed = false;
  };

} // namespace jfd::desktop
