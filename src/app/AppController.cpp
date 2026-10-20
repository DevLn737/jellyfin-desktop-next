#include "AppController.h"
#include "FrameRecorder.h"
#include "MpvPlayer.h"

#include <QQuickWindow>
#include <QScreen>

AppController::AppController(jfd::media::MpvPlayer* player, bool animateMenu, QObject* parent)
  : QObject(parent), m_player(player), m_animateMenu(animateMenu)
{
  m_statsTimer.setInterval(1000);
  connect(&m_statsTimer, &QTimer::timeout, this, &AppController::updateStats);
}

void AppController::attachWindow(QQuickWindow* window, FrameRecorder* frames)
{
  m_window = window;
  m_frames = frames;
  m_statsTimer.start();
}

void AppController::detachWindow()
{
  m_statsTimer.stop();
  m_window = nullptr;
  m_frames = nullptr;
}

void AppController::setStatsVisible(bool visible)
{
  if (visible == m_statsVisible)
    return;
  m_statsVisible = visible;
  emit statsVisibleChanged();
}

void AppController::setGlRenderer(const QString& renderer)
{
  m_glRenderer = renderer;
  emit glRendererChanged();
}

void AppController::toggleFullscreen()
{
  if (!m_window)
    return;
  if (m_window->visibility() == QWindow::FullScreen)
    m_window->showNormal();
  else
    m_window->showFullScreen();
}

void AppController::quit()
{
  if (m_window)
    m_window->close();
}

void AppController::updateStats()
{
  const auto recent = m_frames->takeRecent();
  const double refresh = m_window && m_window->screen() ? m_window->screen()->refreshRate() : 0;
  const auto summary = recent.summary(refresh > 0 ? 1e6 / refresh : 0);
  const auto sample = jfd::diagnostics::sampleCurrentProcess();
  double cpu = 0;
  if (sample && m_lastSample)
    cpu = jfd::diagnostics::cpuPercent(*m_lastSample, *sample);
  m_lastSample = sample;
  const double fps = summary.meanUs > 0 ? 1e6 / summary.meanUs : 0;
  m_statsText = QStringLiteral(
      "%1 fps (экран %2 Гц)  p95 %3 мс  пропуски %4\n"
      "hwdec: %5  avsync: %6 мс\n"
      "CPU %7%  память %8 МиБ\n%9")
    .arg(fps, 0, 'f', 1)
    .arg(refresh, 0, 'f', 0)
    .arg(summary.p95Us / 1000.0, 0, 'f', 1)
    .arg(summary.longFrames)
    .arg(m_player->hwdecCurrent().isEmpty() ? QStringLiteral("нет") : m_player->hwdecCurrent())
    .arg(m_player->avsync() * 1000.0, 0, 'f', 1)
    .arg(cpu, 0, 'f', 0)
    .arg(sample ? static_cast<double>(sample->privateBytes) / (1024.0 * 1024.0) : 0.0, 0, 'f', 0)
    .arg(m_glRenderer);
  emit statsTextChanged();
}
