#include "SmokeTest.h"

#include <jfd/ui/Navigator.h>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>

#include <cstdio>

namespace jfd::desktop {

  namespace {
    void print(const QString& line) {
      std::printf("smoke: %s\n", qPrintable(line));
      std::fflush(stdout);
    }
  } // namespace

  SmokeTest::SmokeTest(QQuickWindow& window, ui::Navigator& navigator, QObject* parent)
      : QObject(parent), m_window(window), m_navigator(navigator) {
    m_routes = {QStringLiteral("home"),     QStringLiteral("library/movies"),
                QStringLiteral("item/abc"), QStringLiteral("collection/abc"),
                QStringLiteral("search"),   QStringLiteral("group"),
                QStringLiteral("settings"), QStringLiteral("player")};
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(QStringLiteral("timed out")); });
  }

  void SmokeTest::start() {
    m_timeout.start();
    // Wait for the first rendered frame with the login page.
    connect(
        &m_window, &QQuickWindow::frameSwapped, this,
        [this] {
          if (m_expected.isEmpty()) {
            m_expected = QStringLiteral("login");
            QTimer::singleShot(0, this, &SmokeTest::step);
          }
        },
        Qt::QueuedConnection);
  }

  QString SmokeTest::shownRoute() const {
    auto* stack = m_window.findChild<QQuickItem*>(QStringLiteral("pageStack"));
    if (stack == nullptr) {
      return QStringLiteral("(no page stack)");
    }
    auto* page = stack->property("currentItem").value<QQuickItem*>();
    return page == nullptr ? QStringLiteral("(no page)") : page->property("route").toString();
  }

  void SmokeTest::step() {
    const QString shown = shownRoute();
    if (shown != m_expected || m_navigator.currentRoute() != m_expected) {
      fail(QStringLiteral("expected %1, navigator at %2, page shows %3")
               .arg(m_expected, m_navigator.currentRoute(), shown));
      return;
    }
    print(QStringLiteral("shows %1 (depth %2)").arg(shown).arg(m_navigator.depth()));
    // JFD_SMOKE_SCREENSHOTS=<dir> saves each page once, for pull requests.
    const QString screenshots = qEnvironmentVariable("JFD_SMOKE_SCREENSHOTS");
    if (!screenshots.isEmpty() && !m_backPhase) {
      QDir().mkpath(screenshots);
      QString name = shown;
      name.replace(QLatin1Char('/'), QLatin1Char('-'));
      m_window.grabWindow().save(screenshots + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    if (!m_backPhase && m_next < m_routes.size()) {
      m_expected = m_routes[m_next++];
      m_navigator.navigate(m_expected);
    } else if (m_navigator.depth() > 1) {
      m_backPhase = true;
      m_navigator.back();
      m_expected = m_navigator.currentRoute();
    } else {
      m_passed = true;
      m_timeout.stop();
      print(QStringLiteral("passed"));
      QCoreApplication::quit();
      return;
    }
    // Let the StackView finish its transition before checking.
    QTimer::singleShot(400, this, &SmokeTest::step);
  }

  void SmokeTest::fail(const QString& reason) {
    print(QStringLiteral("FAILED: ") + reason);
    m_timeout.stop();
    QCoreApplication::exit(1);
  }

} // namespace jfd::desktop
