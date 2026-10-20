#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class QWindow;

// System media controls: media keys, the Windows media overlay (SMTC) and
// MPRIS on Linux (desktop widgets, playerctl, KDE Connect). The session pushes
// what is playing as a state; the controls send back a narrow set of commands.
// SyncPlay groups take the commands like the player's own buttons, so they go
// through the session, never to mpv directly.
namespace jfd::platform {

  struct MediaControlsState {
    enum class Status { Stopped, Playing, Paused };

    Status status = Status::Stopped;
    QString title;    // Episode or movie name.
    QString subtitle; // Series and episode number, or the year.
    QUrl artUrl;      // Poster; must load without credentials.
    qint64 durationMs = 0;
    qint64 positionMs = 0;
    double rate = 1.0;
    bool canSeek = false;
    bool canGoNext = false;
    bool canGoPrevious = false;

    bool operator==(const MediaControlsState&) const = default;
  };

  enum class MediaCommand { Play, Pause, PlayPause, Stop, Next, Previous };

  class MediaControls : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~MediaControls() override = default;

    // False where the platform has no media controls service.
    virtual bool isAvailable() const = 0;
    // The window the controls belong to (Windows needs it for SMTC).
    virtual void attachWindow(QWindow* window) = 0;
    virtual void setState(const MediaControlsState& state) = 0;

  Q_SIGNALS:
    void commandRequested(jfd::platform::MediaCommand command);
    // Absolute position in milliseconds.
    void seekRequested(qint64 positionMs);
    void raiseRequested();
    void quitRequested();
  };

  // MPRIS on Linux, SMTC on Windows; controls that do nothing elsewhere.
  // applicationName is shown by desktops (MPRIS Identity), desktopEntry is the
  // .desktop file name without extension.
  std::unique_ptr<MediaControls> createSystemMediaControls(const QString& applicationName,
                                                           const QString& desktopEntry,
                                                           QObject* parent = nullptr);

} // namespace jfd::platform
