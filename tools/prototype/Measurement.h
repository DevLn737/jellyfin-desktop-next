#pragma once

#include <jfd/diagnostics/ProcessStats.h>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QStringList>
#include <QTimer>

#include <optional>
#include <vector>

class FrameRecorder;
class QQuickWindow;
namespace jfd::media { class MpvPlayer; }

// Scripted measurement runs for the phase 1 thresholds. One scenario per
// process; the result is one JSON report. See scripts/measure/README.md.
struct MeasurementOptions
{
  QString scenario;           // frames, memory, fullscreen, seek, play
  QStringList files;
  int durationSeconds = 0;    // frames, memory, play; 0 = until closed
  int warmupSeconds = 0;      // frames: before recording; memory: growth baseline
  int sampleSeconds = 5;      // memory sample interval
  int replaceSeconds = 0;     // memory: load the next file every N seconds
  int toggles = 50;           // fullscreen round trips
  int seeks = 20;
  QString reportPath;
};

class Measurement : public QObject
{
  Q_OBJECT
public:
  Measurement(MeasurementOptions options, jfd::media::MpvPlayer* player,
              QQuickWindow* window, FrameRecorder* frames, QObject* parent = nullptr);

  void start();
  void setGlRenderer(const QString& renderer) { m_glRenderer = renderer; }
  // Writes the report; called when the run ends and again when the
  // application quits (an interrupted run still leaves its data).
  void writeReport();

private:
  void onFirstFrame(double milliseconds);
  void beginRecording();
  void finish(const QString& outcome);
  void sampleSecond();
  void sampleMemory();
  void nextFile();
  void nextFullscreenStep();
  void nextSeek();
  QJsonObject environment() const;
  QJsonObject frameResults() const;
  double refreshRate() const;

  MeasurementOptions m_options;
  jfd::media::MpvPlayer* m_player;
  QQuickWindow* m_window;
  FrameRecorder* m_frames;
  QString m_glRenderer;
  QString m_outcome = QStringLiteral("running");
  bool m_finished = false;
  bool m_recording = false;

  QElapsedTimer m_clock;
  QTimer m_secondTimer;
  QTimer m_memoryTimer;
  QTimer m_replaceTimer;
  int m_fileIndex = 0;

  QJsonArray m_firstFrames;
  QJsonArray m_seekTimes;
  QJsonArray m_errors;
  std::vector<double> m_avsyncAbsMs;
  std::optional<jfd::diagnostics::ProcessSample> m_recordStart;
  std::optional<jfd::diagnostics::ProcessSample> m_recordEnd;
  QJsonObject m_mpvCountersStart;
  QJsonObject m_mpvCountersEnd;
  QJsonArray m_memorySamples;
  QJsonArray m_hwdecSeen;

  // Fullscreen
  int m_toggle = 0;
  bool m_fullscreenNext = true;
  QHash<QString, QRect> m_normalGeometry;
  QJsonArray m_fullscreenSteps;
  int m_geometryDrifts = 0;
  int m_fullscreenMismatches = 0;

  // Seek
  int m_seek = 0;
  int m_seekFailures = 0;
  QTimer m_seekTimeout;
};
