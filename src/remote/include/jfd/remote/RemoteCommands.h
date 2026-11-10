#pragma once

#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <variant>

// The session messages a Jellyfin server sends to a client it controls
// ("Play on" in Jellyfin Web and the phone apps), as plain values. The
// parser knows every command the server can send; what the client carries
// out, and so advertises, is decided by RemoteControl.
namespace jfd::remote {

  // MessageType "Play": PlayRequest.
  enum class PlayCommand { PlayNow, PlayNext, PlayLast, PlayShuffle, PlayInstantMix };

  struct PlayRequest {
    PlayCommand command = PlayCommand::PlayNow;
    QStringList itemIds;
    // The entry of itemIds that plays first.
    int startIndex = 0;
    std::int64_t startPositionTicks = 0;
    QString mediaSourceId;
    std::optional<int> audioStreamIndex;
    std::optional<int> subtitleStreamIndex;
  };

  // MessageType "Playstate": PlaystateRequest.
  enum class PlaystateCommand { Stop, Pause, Unpause, NextTrack, PreviousTrack, Seek, Rewind, FastForward, PlayPause };

  struct PlaystateRequest {
    PlaystateCommand command = PlaystateCommand::PlayPause;
    // Seek only.
    std::optional<std::int64_t> seekPositionTicks;
  };

  // MessageType "GeneralCommand": a GeneralCommandType name ("SetVolume")
  // and its string arguments ({"Volume": "40"}).
  struct GeneralCommand {
    QString name;
    QHash<QString, QString> arguments;
  };

  using ServerMessage = std::variant<PlayRequest, PlaystateRequest, GeneralCommand>;

  struct ParsedMessage {
    // Empty for messages that are not remote control (and for malformed ones).
    std::optional<ServerMessage> message;
    // Why a remote-control message could not be read; empty otherwise.
    QString error;
  };

  // messageType and data as SessionSocket::messageReceived gives them.
  ParsedMessage parseServerMessage(const QString& messageType, const QJsonValue& data);

  // The server's GeneralCommandType names (Jellyfin 10.10 to 12.2).
  const QStringList& knownGeneralCommands();
  bool isKnownGeneralCommand(const QString& name);

  // The second-start commands: `jellyfin-desktop --command play-pause`.
  struct LocalCommand {
    enum class Kind { PlayPause, Next, Previous, Stop, SeekBy, SeekTo, Fullscreen };
    Kind kind = Kind::PlayPause;
    // SeekBy (may be negative) and SeekTo.
    std::int64_t milliseconds = 0;
  };

  // "play-pause", "next", "previous", "stop", "seek:+30", "seek:-10",
  // "seek:90" (seconds), "fullscreen"; nullopt for anything else.
  std::optional<LocalCommand> parseLocalCommand(const QString& text);
  // The command names for help and error messages.
  QStringList localCommandNames();
  // The value of --command in a command line (`--command x` or
  // `--command=x`); nullopt when it has none. An empty value is returned as
  // an empty string.
  std::optional<QString> commandArgument(const QStringList& arguments);

} // namespace jfd::remote
