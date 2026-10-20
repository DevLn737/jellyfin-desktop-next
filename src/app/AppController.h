#pragma once

#include <jfd/diagnostics/ProcessStats.h>

#include <QObject>
#include <QTimer>

#include <optional>

class FrameRecorder;
class QQuickWindow;
namespace jfd::media { class MpvPlayer; }

// What QML may ask of the application: window state and on-screen statistics.
class AppController : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool animateMenu READ animateMenu CONSTANT)
  Q_PROPERTY(bool statsVisible READ statsVisible WRITE setStatsVisible NOTIFY statsVisibleChanged)
  Q_PROPERTY(QString statsText READ statsText NOTIFY statsTextChanged)
  Q_PROPERTY(QString glRenderer READ glRenderer NOTIFY glRendererChanged)

public:
  AppController(jfd::media::MpvPlayer* player, bool animateMenu, QObject* parent = nullptr);

  void attachWindow(QQuickWindow* window, FrameRecorder* frames);
  void detachWindow();

  bool animateMenu() const { return m_animateMenu; }
  bool statsVisible() const { return m_statsVisible; }
  void setStatsVisible(bool visible);
  QString statsText() const { return m_statsText; }
  QString glRenderer() const { return m_glRenderer; }
  void setGlRenderer(const QString& renderer);

  Q_INVOKABLE void toggleFullscreen();
  Q_INVOKABLE void quit();

signals:
  void statsVisibleChanged();
  void statsTextChanged();
  void glRendererChanged();

private:
  void updateStats();

  jfd::media::MpvPlayer* m_player;
  QQuickWindow* m_window = nullptr;
  FrameRecorder* m_frames = nullptr;
  bool m_animateMenu;
  bool m_statsVisible = false;
  QString m_statsText;
  QString m_glRenderer;
  QTimer m_statsTimer;
  std::optional<jfd::diagnostics::ProcessSample> m_lastSample;
};
