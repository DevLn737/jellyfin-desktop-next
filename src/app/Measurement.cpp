#include "Measurement.h"
#include "FrameRecorder.h"
#include "MpvPlayer.h"

#include <jfd/diagnostics/FrameStats.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QScreen>
#include <QSysInfo>

#include <algorithm>
#include <cmath>
#include <thread>

#include <mpv/client.h>

using jfd::diagnostics::FrameStats;
using jfd::diagnostics::cpuPercent;
using jfd::diagnostics::sampleCurrentProcess;

namespace {
// Settle time after a window state change before its geometry is read.
constexpr int kWindowSettleMs = 700;
constexpr int kSeekPauseMs = 700;
constexpr int kSeekTimeoutMs = 5000;

QJsonObject rectJson(const QRect& rect)
{
  return {{"x", rect.x()}, {"y", rect.y()}, {"w", rect.width()}, {"h", rect.height()}};
}

double toDouble(const QVariant& value)
{
  return value.isValid() ? value.toDouble() : -1;
}
}

Measurement::Measurement(MeasurementOptions options, jfd::media::MpvPlayer* player,
                         QQuickWindow* window, FrameRecorder* frames, QObject* parent)
  : QObject(parent), m_options(std::move(options)), m_player(player), m_window(window), m_frames(frames)
{
  m_secondTimer.setInterval(1000);
  connect(&m_secondTimer, &QTimer::timeout, this, &Measurement::sampleSecond);
  m_memoryTimer.setInterval(std::max(1, m_options.sampleSeconds) * 1000);
  connect(&m_memoryTimer, &QTimer::timeout, this, &Measurement::sampleMemory);
  m_replaceTimer.setInterval(std::max(1, m_options.replaceSeconds) * 1000);
  connect(&m_replaceTimer, &QTimer::timeout, this, &Measurement::nextFile);
  m_seekTimeout.setSingleShot(true);
  m_seekTimeout.setInterval(kSeekTimeoutMs);
  connect(&m_seekTimeout, &QTimer::timeout, this, [this] {
    ++m_seekFailures;
    m_seekTimes.append(QJsonValue::Null);
    nextSeek();
  });

  connect(player, &jfd::media::MpvPlayer::firstFrame, this, &Measurement::onFirstFrame);
  connect(player, &jfd::media::MpvPlayer::seekCompleted, this, [this](double ms) {
    if (m_options.scenario != QLatin1String("seek") || !m_seekTimeout.isActive())
      return;
    m_seekTimeout.stop();
    m_seekTimes.append(ms);
    QTimer::singleShot(kSeekPauseMs, this, &Measurement::nextSeek);
  });
  connect(player, &jfd::media::MpvPlayer::error, this, [this](const QString& message) {
    m_errors.append(QStringLiteral("%1 s: %2").arg(static_cast<double>(m_clock.elapsed()) / 1000.0, 0, 'f', 1).arg(message));
  });
  connect(player, &jfd::media::MpvPlayer::hwdecCurrentChanged, this, [this] {
    const QString value = m_player->hwdecCurrent();
    if (!value.isEmpty() && !m_hwdecSeen.contains(value))
      m_hwdecSeen.append(value);
  });
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
    if (!m_finished)
    {
      m_outcome = QStringLiteral("closed");
      m_recordEnd = sampleCurrentProcess();
      m_mpvCountersEnd = {
        {"frameDropCount", toDouble(m_player->property("frame-drop-count"))},
        {"voDelayedFrameCount", toDouble(m_player->property("vo-delayed-frame-count"))},
        {"mistimedFrameCount", toDouble(m_player->property("mistimed-frame-count"))},
      };
    }
    writeReport();
  });
}

void Measurement::start()
{
  m_clock.start();
  if (m_options.files.isEmpty())
  {
    finish(QStringLiteral("no file"));
    return;
  }
  m_player->load(m_options.files.first());
  if (m_options.scenario == QLatin1String("memory"))
  {
    m_memoryTimer.start();
    sampleMemory();
  }
}

void Measurement::onFirstFrame(double milliseconds)
{
  m_firstFrames.append(milliseconds);
  if (m_recording || m_finished)
    return;
  if (m_options.scenario == QLatin1String("memory") && m_firstFrames.size() > 1)
    return;
  const int warmup = m_options.scenario == QLatin1String("frames") ? m_options.warmupSeconds * 1000 : 0;
  QTimer::singleShot(std::max(warmup, 1000), this, &Measurement::beginRecording);
}

void Measurement::beginRecording()
{
  if (m_recording || m_finished)
    return;
  m_recording = true;
  const QString scenario = m_options.scenario;
  if (scenario == QLatin1String("fullscreen"))
  {
    nextFullscreenStep();
    return;
  }
  if (scenario == QLatin1String("seek"))
  {
    nextSeek();
    return;
  }
  // frames, memory, play: continuous playback for the given duration.
  m_frames->restart();
  m_recordStart = sampleCurrentProcess();
  m_mpvCountersStart = {
    {"frameDropCount", toDouble(m_player->property("frame-drop-count"))},
    {"voDelayedFrameCount", toDouble(m_player->property("vo-delayed-frame-count"))},
    {"mistimedFrameCount", toDouble(m_player->property("mistimed-frame-count"))},
  };
  m_secondTimer.start();
  if (scenario == QLatin1String("memory") && m_options.replaceSeconds > 0)
    m_replaceTimer.start();
  if (m_options.durationSeconds > 0)
    QTimer::singleShot(m_options.durationSeconds * 1000, this, [this] { finish(QStringLiteral("completed")); });
}

void Measurement::sampleSecond()
{
  const auto paused = m_player->paused();
  if (!paused)
    m_avsyncAbsMs.push_back(std::abs(m_player->avsync()) * 1000.0);
}

void Measurement::sampleMemory()
{
  const auto sample = sampleCurrentProcess();
  if (!sample)
    return;
  m_memorySamples.append(QJsonObject {
    {"t", static_cast<double>(m_clock.elapsed()) / 1000.0},
    {"residentBytes", static_cast<double>(sample->residentBytes)},
    {"privateBytes", static_cast<double>(sample->privateBytes)},
  });
}

void Measurement::nextFile()
{
  if (m_options.files.isEmpty())
    return;
  m_fileIndex = (m_fileIndex + 1) % static_cast<int>(m_options.files.size());
  m_player->load(m_options.files.at(m_fileIndex));
}

void Measurement::nextFullscreenStep()
{
  if (m_finished)
    return;
  QScreen* screen = m_window->screen();
  const QString screenName = screen ? screen->name() : QString();
  if (m_fullscreenNext)
  {
    if (m_toggle >= m_options.toggles)
    {
      finish(QStringLiteral("completed"));
      return;
    }
    // Spread round trips over all screens so different DPI settings are covered.
    const auto screens = QGuiApplication::screens();
    QScreen* target = screens.value(m_toggle % static_cast<int>(screens.size()), screen);
    if (target && target != screen)
    {
      const QRect available = target->availableGeometry();
      const QSize size = m_window->size().boundedTo(available.size() * 0.8);
      m_window->setScreen(target);
      m_window->setGeometry(QRect(available.center() - QPoint(size.width() / 2, size.height() / 2), size));
      QTimer::singleShot(kWindowSettleMs, this, &Measurement::nextFullscreenStep);
      return;
    }
    if (!m_normalGeometry.contains(screenName))
      m_normalGeometry.insert(screenName, m_window->geometry());
    m_window->showFullScreen();
  }
  else
  {
    m_window->showNormal();
  }
  m_fullscreenNext = !m_fullscreenNext;
  QTimer::singleShot(kWindowSettleMs, this, [this] {
    QScreen* current = m_window->screen();
    const QString name = current ? current->name() : QString();
    const QRect geometry = m_window->geometry();
    const bool fullscreen = !m_fullscreenNext; // the step just taken
    bool ok = m_window->isVisible() && m_window->isExposed();
    if (fullscreen)
    {
      ok = ok && current && geometry == current->geometry()
        && m_window->visibility() == QWindow::FullScreen;
      if (!ok)
        ++m_fullscreenMismatches;
    }
    else
    {
      ok = ok && geometry == m_normalGeometry.value(name, geometry);
      if (!ok)
        ++m_geometryDrifts;
      ++m_toggle;
    }
    m_fullscreenSteps.append(QJsonObject {
      {"fullscreen", fullscreen},
      {"screen", name},
      {"dpr", current ? current->devicePixelRatio() : 0},
      {"geometry", rectJson(geometry)},
      {"ok", ok},
    });
    nextFullscreenStep();
  });
}

void Measurement::nextSeek()
{
  if (m_finished)
    return;
  if (m_seek >= m_options.seeks)
  {
    finish(QStringLiteral("completed"));
    return;
  }
  const double duration = m_player->duration();
  // A fixed spread of targets over the first 90% of the file, same on every run.
  const double fraction = std::fmod(0.05 + 0.37 * m_seek, 0.9);
  ++m_seek;
  m_seekTimeout.start();
  m_player->seek(duration > 0 ? duration * fraction : 0);
}

void Measurement::finish(const QString& outcome)
{
  if (m_finished)
    return;
  m_finished = true;
  m_outcome = outcome;
  m_secondTimer.stop();
  m_replaceTimer.stop();
  m_memoryTimer.stop();
  m_recordEnd = sampleCurrentProcess();
  m_mpvCountersEnd = {
    {"frameDropCount", toDouble(m_player->property("frame-drop-count"))},
    {"voDelayedFrameCount", toDouble(m_player->property("vo-delayed-frame-count"))},
    {"mistimedFrameCount", toDouble(m_player->property("mistimed-frame-count"))},
  };
  if (m_options.scenario == QLatin1String("memory"))
    sampleMemory();
  writeReport();
  m_window->close();
}

double Measurement::refreshRate() const
{
  return m_window->screen() ? m_window->screen()->refreshRate() : 0;
}

QJsonObject Measurement::environment() const
{
  QJsonArray screens;
  for (QScreen* screen : QGuiApplication::screens())
    screens.append(QJsonObject {
      {"name", screen->name()},
      {"geometry", rectJson(screen->geometry())},
      {"devicePixelRatio", screen->devicePixelRatio()},
      {"refreshRate", screen->refreshRate()},
    });
  return {
    {"os", QSysInfo::prettyProductName()},
    {"cpuCores", static_cast<int>(std::thread::hardware_concurrency())},
    {"qt", QString::fromLatin1(qVersion())},
    {"mpv", m_player->property("mpv-version").toString()},
    {"glRenderer", m_glRenderer},
    {"screens", screens},
    {"windowScreen", m_window->screen() ? m_window->screen()->name() : QString()},
  };
}

QJsonObject Measurement::frameResults() const
{
  const FrameStats frames = m_frames->measurement();
  const double refresh = refreshRate();
  const double period = refresh > 0 ? 1e6 / refresh : 0;
  const auto summary = frames.summary(period);
  std::vector<std::int64_t> avsync;
  for (double value : m_avsyncAbsMs)
    avsync.push_back(static_cast<std::int64_t>(std::llround(value * 1000.0)));
  QJsonObject result {
    {"refreshRateHz", refresh},
    {"refreshPeriodUs", period},
    {"intervals", static_cast<double>(summary.frames)},
    {"meanUs", summary.meanUs},
    {"p50Us", summary.p50Us},
    {"p95Us", summary.p95Us},
    {"p99Us", summary.p99Us},
    {"maxUs", summary.maxUs},
    {"longFrames", static_cast<double>(summary.longFrames)},
    {"p95Periods", period > 0 ? summary.p95Us / period : 0},
    {"avsyncAbsMsP95", jfd::diagnostics::percentile(avsync, 95) / 1000.0},
    {"avsyncAbsMsMax", avsync.empty() ? 0.0 : static_cast<double>(*std::max_element(avsync.begin(), avsync.end())) / 1000.0},
    {"mpvCountersStart", m_mpvCountersStart},
    {"mpvCountersEnd", m_mpvCountersEnd},
  };
  if (m_recordStart && m_recordEnd)
  {
    result.insert("cpuPercentOfOneCore", cpuPercent(*m_recordStart, *m_recordEnd));
    result.insert("recordedSeconds", static_cast<double>(m_recordEnd->timestampUs - m_recordStart->timestampUs) / 1e6);
  }
  return result;
}

void Measurement::writeReport()
{
  if (m_options.reportPath.isEmpty())
    return;
  QJsonObject report {
    {"schemaVersion", 1},
    {"client", "jellyfin-desktop-next"},
    {"scenario", m_options.scenario},
    {"outcome", m_outcome},
    {"writtenAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    {"files", QJsonArray::fromStringList(m_options.files)},
    {"environment", environment()},
    {"hwdecCurrent", m_hwdecSeen},
    {"videoParams", QJsonObject::fromVariantMap(m_player->property("video-params").toMap())},
    {"firstFrameMs", m_firstFrames},
    {"errors", m_errors},
  };
  const QString scenario = m_options.scenario;
  if (scenario == QLatin1String("frames") || scenario == QLatin1String("play") || scenario == QLatin1String("memory"))
    report.insert("frames", frameResults());
  if (scenario == QLatin1String("memory"))
  {
    QJsonObject memory {
      {"warmupSeconds", m_options.warmupSeconds},
      {"sampleSeconds", m_options.sampleSeconds},
      {"replaceSeconds", m_options.replaceSeconds},
      {"samples", m_memorySamples},
    };
    // Growth from the first sample at or after the warm-up to the last one.
    double baseline = -1;
    double last = -1;
    double peak = 0;
    for (const auto& value : m_memorySamples)
    {
      const auto sample = value.toObject();
      const double bytes = sample.value("privateBytes").toDouble();
      peak = std::max(peak, bytes);
      if (baseline < 0 && sample.value("t").toDouble() >= m_options.warmupSeconds)
        baseline = bytes;
      last = bytes;
    }
    if (baseline >= 0)
      memory.insert("privateGrowthBytes", last - baseline);
    memory.insert("privatePeakBytes", peak);
    report.insert("memory", memory);
  }
  if (scenario == QLatin1String("fullscreen"))
    report.insert("fullscreen", QJsonObject {
      {"roundTrips", m_toggle},
      {"geometryDrifts", m_geometryDrifts},
      {"fullscreenMismatches", m_fullscreenMismatches},
      {"steps", m_fullscreenSteps},
    });
  if (scenario == QLatin1String("seek"))
  {
    std::vector<std::int64_t> times;
    for (const auto& value : m_seekTimes)
      if (value.isDouble())
        times.push_back(static_cast<std::int64_t>(std::llround(value.toDouble() * 1000.0)));
    report.insert("seek", QJsonObject {
      {"timesMs", m_seekTimes},
      {"failures", m_seekFailures},
      {"p95Ms", jfd::diagnostics::percentile(times, 95) / 1000.0},
      {"maxMs", times.empty() ? 0.0 : static_cast<double>(*std::max_element(times.begin(), times.end())) / 1000.0},
    });
  }
  QFile file(m_options.reportPath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    file.write(QJsonDocument(report).toJson());
}
