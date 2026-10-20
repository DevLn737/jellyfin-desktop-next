#pragma once

#include "MpvHandle.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariant>

namespace jfd::media {

// Owns the mpv core and is its only client-event consumer. Lives on the GUI
// thread; mpv wakes it through a queued call.
class MpvPlayer : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
  Q_PROPERTY(double position READ position NOTIFY positionChanged)
  Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
  Q_PROPERTY(QString hwdecCurrent READ hwdecCurrent NOTIFY hwdecCurrentChanged)
  Q_PROPERTY(QString title READ title NOTIFY titleChanged)

public:
  struct Options
  {
    QString hwdec = QStringLiteral("auto-safe");
    QString audioOutput; // empty: mpv default
    bool loopFile = false;
  };

  explicit MpvPlayer(const Options& options, QObject* parent = nullptr);
  ~MpvPlayer() override;

  bool isValid() const { return static_cast<bool>(m_mpv); }
  const MpvHandle& handle() const { return m_mpv; }

  Q_INVOKABLE void load(const QString& url);
  Q_INVOKABLE void seek(double seconds);
  Q_INVOKABLE void seekRelative(double seconds);
  Q_INVOKABLE void togglePause() { setPaused(!m_paused); }
  void setPaused(bool paused);
  // Stops playback; the core stays alive until destruction.
  void stop();

  bool paused() const { return m_paused; }
  double position() const { return m_position; }
  double duration() const { return m_duration; }
  QString hwdecCurrent() const { return m_hwdecCurrent; }
  QString title() const { return m_title; }
  double avsync() const { return m_avsync; }

  // Reads any mpv property synchronously (diagnostics only).
  QVariant property(const char* name) const;

signals:
  void pausedChanged();
  void positionChanged();
  void durationChanged();
  void hwdecCurrentChanged();
  void titleChanged();
  // Time from loadfile to the first playback-restart (first frame ready).
  void firstFrame(double milliseconds);
  // Time from a seek command to its playback-restart.
  void seekCompleted(double milliseconds);
  void endOfFile(QString reason);
  void error(QString message);

private:
  void drainEvents();
  void handleProperty(const mpv_event_property& property);
  void command(std::initializer_list<const char*> arguments);

  MpvHandle m_mpv;
  bool m_paused = false;
  double m_position = 0;
  double m_duration = 0;
  double m_avsync = 0;
  QString m_hwdecCurrent;
  QString m_title;
  QElapsedTimer m_loadTimer;
  QElapsedTimer m_seekTimer;
  bool m_awaitingFirstFrame = false;
  bool m_awaitingSeek = false;
};

}
