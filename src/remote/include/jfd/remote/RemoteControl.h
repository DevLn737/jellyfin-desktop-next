#pragma once

#include <jfd/core/SessionInput.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/remote/RemoteCommands.h>

#include <QObject>
#include <QStringList>

#include <functional>

namespace jfd::api {
  struct SessionCapabilities;
}

// Carries out the server's remote-control messages (and the second-start
// --command commands) through a narrow set of actions the application
// connects, so this module knows nothing of the screens or the player.
//
// Playstate goes the way of the system media controls
// (integrations::commandInput, then the session's post): in a SyncPlay
// group it becomes a request to the group. Play goes to the group's queue
// in a group and to the app's launcher outside one.
namespace jfd::remote {

  // Where GoHome, GoToSearch and GoToSettings lead.
  enum class Destination { Home, Search, Settings };

  // MoveUp ... Back as the input router delivers them.
  enum class NavigationKey { Up, Down, Left, Right, PageUp, PageDown, Select, Back };

  // SetRepeatMode's values: RepeatNone, RepeatAll, RepeatOne.
  enum class RepeatMode { None, All, One };

  // What the application can do. An action left empty is not offered: the
  // general commands that need it are left out of SupportedCommands and
  // ignored when they arrive anyway.
  struct RemoteActions {
    // Play outside a group: resolves the items and plays or queues them.
    std::function<void(const PlayRequest&)> play;
    // Play in a group: PlayNow and PlayShuffle as the group's new queue,
    // PlayNext and PlayLast added to its queue.
    std::function<void(const PlayRequest&)> playInGroup;

    // 0..100.
    std::function<int()> volume;
    std::function<void(int)> setVolume;
    std::function<bool()> muted;
    std::function<void(bool)> setMuted;
    // The server's stream index; -1 turns subtitles off.
    std::function<void(int)> selectAudio;
    std::function<void(int)> selectSubtitle;
    // DisplayMessage: header and text as the sender wrote them.
    std::function<void(const QString& header, const QString& text)> showMessage;
    std::function<void()> toggleFullscreen;
    std::function<void(Destination)> goTo;
    // Through the shell's input router (MoveUp ... Back).
    std::function<void(NavigationKey)> navigate;
    // SendString: text typed on the phone, into the focused field.
    std::function<void(const QString&)> sendText;
    std::function<void(RepeatMode)> setRepeatMode;
    std::function<void(bool)> setShuffle;
    // Bits per second; 0 for no limit.
    std::function<void(qint64)> setMaxBitrate;
  };

  // No Q_OBJECT: it has no signals, and moc of older Qt cannot parse core's
  // C++20 headers. A QObject so that connections can use it as context.
  class RemoteControl final : public QObject {
  public:
    using Post = std::function<void(core::SessionInput)>;
    using SnapshotSource = std::function<core::SessionSnapshot()>;

    enum class Outcome {
      Done,
      // Not a remote-control message.
      NotRemote,
      // Remote control is turned off in the settings.
      Disabled,
      // Read, but the client does not do this (not advertised).
      Unsupported,
      // Could not be read.
      Malformed,
    };

    // post: session::SessionRuntime::post; snapshot: its current snapshot.
    RemoteControl(Post post, SnapshotSource snapshot, RemoteActions actions, QObject* parent = nullptr);

    // remote/allowServerControl. Off: server messages are ignored and the
    // capabilities say the client cannot be controlled. --command works
    // either way: it comes from this user's own computer.
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }

    // The GeneralCommand names this client carries out, from the same table
    // that handles them; empty when turned off.
    QStringList supportedCommands() const;
    // SupportsMediaControl and SupportedCommands.
    void applyTo(api::SessionCapabilities& capabilities) const;

    // A SessionSocket::messageReceived message.
    Outcome handle(const QString& messageType, const QJsonValue& data);
    Outcome handle(const ServerMessage& message);
    // A second start's --command.
    Outcome run(const LocalCommand& command);

    // Rewind and FastForward step back and forward by these.
    static constexpr std::int64_t RewindMs = 10'000;
    static constexpr std::int64_t FastForwardMs = 30'000;
    // VolumeUp and VolumeDown.
    static constexpr int VolumeStep = 5;

  private:
    Outcome play(const PlayRequest& request);
    Outcome playstate(const PlaystateRequest& request);
    Outcome general(const GeneralCommand& command);
    void seekTo(std::int64_t milliseconds);
    void seekBy(std::int64_t milliseconds);

    Post m_post;
    SnapshotSource m_snapshot;
    RemoteActions m_actions;
    bool m_enabled = true;
  };

} // namespace jfd::remote
