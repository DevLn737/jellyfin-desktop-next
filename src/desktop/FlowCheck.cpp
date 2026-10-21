#include "FlowCheck.h"

#include "AppServices.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/SignIn.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/PlayerModel.h>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantMap>

#include <cstdio>

namespace jfd::desktop {

  namespace {
    void print(const QString& line) {
      std::printf("flow: %s\n", qPrintable(line));
      std::fflush(stdout);
    }

    constexpr int PageMs = 20000;
    // Includes the server's PlaybackInfo and mpv opening the stream.
    constexpr int PlaybackMs = 30000;
    constexpr int TrackMs = 15000;
  } // namespace

  FlowCheck::FlowCheck(QQuickWindow& window, AppServices& services, QObject* parent)
      : QObject(parent), m_window(window), m_services(services) {
    m_poll.setInterval(100);
    connect(&m_poll, &QTimer::timeout, this, [this] {
      if (m_failed || !m_check) return;
      if (m_check()) {
        m_poll.stop();
        m_check = nullptr;
        auto next = std::move(m_next);
        m_next = nullptr;
        next();
      } else if (m_waitClock.elapsed() > m_timeoutMs) {
        fail(QStringLiteral("timed out waiting for ") + m_waiting);
      }
    });
  }

  bool FlowCheck::configured() {
    return !qEnvironmentVariable("JFD_TEST_SERVER_URL").isEmpty() &&
           !qEnvironmentVariable("JFD_TEST_USER").isEmpty();
  }

  void FlowCheck::start() {
    m_stepClock.start();
    waitFor(QStringLiteral("the login page"), [this] { return shows(QStringLiteral("login")); }, PageMs, [this] {
      done(QStringLiteral("login page shown"));
      screenshot(QStringLiteral("1-login"));
      signIn();
    });
  }

  void FlowCheck::waitFor(const QString& what, Check check, int timeoutMs, Next next) {
    m_waiting = what;
    m_check = std::move(check);
    m_next = std::move(next);
    m_timeoutMs = timeoutMs;
    m_waitClock.start();
    m_poll.start();
  }

  void FlowCheck::done(const QString& what) {
    print(QStringLiteral("%1 (%2 ms)").arg(what).arg(m_stepClock.restart()));
  }

  void FlowCheck::fail(const QString& reason) {
    if (m_failed) return;
    m_failed = true;
    m_poll.stop();
    print(QStringLiteral("FAILED: ") + reason);
    screenshot(QStringLiteral("failure"));
    QCoreApplication::exit(1);
  }

  void FlowCheck::screenshot(const QString& name) {
    const QString dir = qEnvironmentVariable("JFD_SMOKE_SCREENSHOTS");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    m_window.grabWindow().save(dir + QStringLiteral("/flow-") + name + QStringLiteral(".png"));
  }

  QString FlowCheck::shownRoute() const {
    auto* stack = m_window.findChild<QQuickItem*>(QStringLiteral("pageStack"));
    auto* page = stack == nullptr ? nullptr : stack->property("currentItem").value<QQuickItem*>();
    return page == nullptr ? QString() : page->property("route").toString();
  }

  bool FlowCheck::shows(const QString& route) const {
    // The page is current and the stack has finished moving to it.
    auto* stack = m_window.findChild<QQuickItem*>(QStringLiteral("pageStack"));
    return stack != nullptr && !stack->property("busy").toBool() && shownRoute() == route &&
           m_services.navigator->currentRoute() == route;
  }

  void FlowCheck::signIn() {
    library::SignIn& signIn = *m_services.signIn;
    signIn.connectToServer(qEnvironmentVariable("JFD_TEST_SERVER_URL"));
    waitFor(
        QStringLiteral("the server check"),
        [&signIn] { return signIn.step() == library::SignIn::Step::Credentials || !signIn.errorText().isEmpty(); },
        PageMs, [this, &signIn] {
          if (signIn.step() != library::SignIn::Step::Credentials) {
            return fail(QStringLiteral("server check: ") + signIn.errorText());
          }
          done(QStringLiteral("server %1 checked").arg(signIn.serverName()));
          signIn.signIn(qEnvironmentVariable("JFD_TEST_USER"), qEnvironmentVariable("JFD_TEST_PASSWORD"));
          waitFor(QStringLiteral("home after sign-in"), [this] { return shows(QStringLiteral("home")); }, PageMs,
                  [this] {
                    done(QStringLiteral("signed in, home shown"));
                    // Let the rows load their first cards for the picture.
                    QTimer::singleShot(1500, this, [this] {
                      screenshot(QStringLiteral("2-home"));
                      openSeries();
                    });
                  });
        });
  }

  void FlowCheck::openSeries() {
    api::ItemQuery query;
    query.includeItemTypes = {QStringLiteral("Series")};
    query.limit = 1;
    m_services.client->items(query, this, [this](api::Result<api::ItemPage> page) {
      if (!page || page->items.isEmpty()) return fail(QStringLiteral("no series on the server"));
      const QString seriesId = page->items.first().id;
      const QString route = QStringLiteral("item/") + seriesId;
      m_services.navigator->navigate(route);
      waitFor(QStringLiteral("the series page"), [this, route] { return shows(route); }, PageMs,
              [this, seriesId] {
                done(QStringLiteral("series page shown"));
                QTimer::singleShot(1000, this, [this, seriesId] {
                  screenshot(QStringLiteral("3-series"));
                  openEpisode(seriesId);
                });
              });
    });
  }

  void FlowCheck::openEpisode(const QString& seriesId) {
    m_services.client->episodes(seriesId, QString(), this, [this](api::Result<api::ItemPage> page) {
      if (!page || page->items.isEmpty()) return fail(QStringLiteral("the series has no episodes"));
      const QString episodeId = page->items.first().id;
      const QString route = QStringLiteral("item/") + episodeId;
      m_services.navigator->navigate(route);
      waitFor(QStringLiteral("the episode page"), [this, route] { return shows(route); }, PageMs,
              [this, episodeId] {
                done(QStringLiteral("episode page shown"));
                QTimer::singleShot(1000, this, [this, episodeId] {
                  screenshot(QStringLiteral("4-episode"));
                  playEpisode(episodeId);
                });
              });
    });
  }

  void FlowCheck::playEpisode(const QString& episodeId) {
    if (!m_services.libraryUi->canPlay()) return fail(QStringLiteral("Play is not connected to the player"));
    // What the item page's Play button calls.
    m_services.libraryUi->play(episodeId, 0);
    ui::PlayerModel& player = *m_services.playerModel;
    waitFor(
        QStringLiteral("the episode to play"),
        [this, &player] {
          return shows(QStringLiteral("player")) && player.active() && !player.loading() && !player.paused() &&
                 player.position() >= 2.0;
        },
        PlaybackMs, [this, &player] {
          if (!player.error().isEmpty()) return fail(QStringLiteral("player error: ") + player.error());
          done(QStringLiteral("episode \"%1\" playing at %2 s").arg(player.title()).arg(player.position(), 0, 'f', 1));
          // A few frames in, so the picture is on screen.
          QTimer::singleShot(1500, this, [this] {
            screenshot(QStringLiteral("5-episode-playing"));
            playMovie();
          });
        });
  }

  void FlowCheck::playMovie() {
    api::ItemQuery query;
    query.includeItemTypes = {QStringLiteral("Movie")};
    query.limit = 1;
    m_services.client->items(query, this, [this](api::Result<api::ItemPage> page) {
      if (!page || page->items.isEmpty()) return fail(QStringLiteral("no movie on the server"));
      const QString movieId = page->items.first().id;
      const QString title = page->items.first().name;
      m_services.libraryUi->play(movieId, 0);
      ui::PlayerModel& player = *m_services.playerModel;
      waitFor(
          QStringLiteral("the movie to play"),
          [this, &player, title] {
            return shows(QStringLiteral("player")) && player.title().contains(title) && !player.loading() &&
                   player.position() >= 2.0 && player.audioTracks().size() >= 2;
          },
          PlaybackMs, [this, &player] {
            done(QStringLiteral("movie playing with %1 audio and %2 subtitle tracks")
                     .arg(player.audioTracks().size())
                     .arg(player.subtitleTracks().size()));
            switchAudio();
          });
    });
  }

  void FlowCheck::switchAudio() {
    ui::PlayerModel& player = *m_services.playerModel;
    m_wantedIndex = -1;
    for (const QVariant& track : player.audioTracks()) {
      const int index = track.toMap().value(QStringLiteral("index")).toInt();
      if (index != player.audioIndex()) {
        m_wantedIndex = index;
        break;
      }
    }
    if (m_wantedIndex < 0) return fail(QStringLiteral("no second audio track"));
    m_positionBefore = player.position();
    player.selectAudio(m_wantedIndex);
    waitFor(
        QStringLiteral("audio track %1").arg(m_wantedIndex),
        [this, &player] {
          // Switched, and playback went on from where it was.
          return player.audioIndex() == m_wantedIndex && !player.loading() &&
                 player.position() >= m_positionBefore + 1.0;
        },
        TrackMs, [this, &player] {
          done(QStringLiteral("audio switched to track %1, position %2 s -> %3 s")
                   .arg(m_wantedIndex)
                   .arg(m_positionBefore, 0, 'f', 1)
                   .arg(player.position(), 0, 'f', 1));
          switchSubtitles(false);
        });
  }

  void FlowCheck::switchSubtitles(bool external) {
    ui::PlayerModel& player = *m_services.playerModel;
    m_wantedIndex = -1;
    for (const QVariant& track : player.subtitleTracks()) {
      const QVariantMap map = track.toMap();
      if (map.value(QStringLiteral("isExternal")).toBool() == external) {
        m_wantedIndex = map.value(QStringLiteral("index")).toInt();
        break;
      }
    }
    const QString kind = external ? QStringLiteral("external") : QStringLiteral("embedded");
    if (m_wantedIndex < 0) return fail(QStringLiteral("no %1 subtitle track").arg(kind));
    m_positionBefore = player.position();
    player.selectSubtitle(m_wantedIndex);
    waitFor(
        QStringLiteral("%1 subtitles").arg(kind),
        [this, &player] {
          return player.subtitleIndex() == m_wantedIndex && !player.loading() &&
                 player.position() >= m_positionBefore + 1.0;
        },
        TrackMs, [this, &player, external, kind] {
          done(QStringLiteral("%1 subtitles on (track %2), position %3 s -> %4 s")
                   .arg(kind)
                   .arg(m_wantedIndex)
                   .arg(m_positionBefore, 0, 'f', 1)
                   .arg(player.position(), 0, 'f', 1));
          screenshot(external ? QStringLiteral("7-external-subtitles") : QStringLiteral("6-embedded-subtitles"));
          if (!external) {
            switchSubtitles(true);
          } else {
            finish();
          }
        });
  }

  void FlowCheck::finish() {
    m_services.playerModel->stop();
    m_services.navigator->back();
    waitFor(QStringLiteral("leaving the player"),
            [this] { return !shownRoute().isEmpty() && shownRoute() != QLatin1String("player"); }, PageMs, [this] {
              done(QStringLiteral("player closed, back on %1").arg(shownRoute()));
              m_passed = true;
              print(QStringLiteral("passed"));
              QCoreApplication::quit();
            });
  }

} // namespace jfd::desktop
