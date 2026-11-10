#include <jfd/remote/RemoteCommands.h>

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <limits>

namespace jfd::remote {

  namespace {
    // A number, or a number written as a string (some clients send both).
    std::optional<std::int64_t> integer(const QJsonValue& value) {
      if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::trunc(number) != number || std::fabs(number) > 9.0e15) // Exact in a double.
          return std::nullopt;
        return static_cast<std::int64_t>(number);
      }
      if (value.isString()) {
        bool ok = false;
        const qlonglong number = value.toString().trimmed().toLongLong(&ok);
        if (ok) return static_cast<std::int64_t>(number);
      }
      return std::nullopt;
    }

    std::optional<int> smallInteger(const QJsonValue& value) {
      const auto number = integer(value);
      if (!number || *number < std::numeric_limits<int>::min() || *number > std::numeric_limits<int>::max())
        return std::nullopt;
      return static_cast<int>(*number);
    }

    std::optional<PlayCommand> playCommand(const QString& name) {
      if (name == QLatin1String("PlayNow")) return PlayCommand::PlayNow;
      if (name == QLatin1String("PlayNext")) return PlayCommand::PlayNext;
      if (name == QLatin1String("PlayLast")) return PlayCommand::PlayLast;
      if (name == QLatin1String("PlayShuffle")) return PlayCommand::PlayShuffle;
      if (name == QLatin1String("PlayInstantMix")) return PlayCommand::PlayInstantMix;
      return std::nullopt;
    }

    std::optional<PlaystateCommand> playstateCommand(const QString& name) {
      if (name == QLatin1String("Stop")) return PlaystateCommand::Stop;
      if (name == QLatin1String("Pause")) return PlaystateCommand::Pause;
      if (name == QLatin1String("Unpause")) return PlaystateCommand::Unpause;
      if (name == QLatin1String("NextTrack")) return PlaystateCommand::NextTrack;
      if (name == QLatin1String("PreviousTrack")) return PlaystateCommand::PreviousTrack;
      if (name == QLatin1String("Seek")) return PlaystateCommand::Seek;
      if (name == QLatin1String("Rewind")) return PlaystateCommand::Rewind;
      if (name == QLatin1String("FastForward")) return PlaystateCommand::FastForward;
      if (name == QLatin1String("PlayPause")) return PlaystateCommand::PlayPause;
      return std::nullopt;
    }

    ParsedMessage failed(const QString& error) { return ParsedMessage{std::nullopt, error}; }

    ParsedMessage parsePlay(const QJsonObject& data) {
      PlayRequest request;
      const QJsonValue command = data.value(QStringLiteral("PlayCommand"));
      // The server's default is PlayNow.
      if (!command.isUndefined() && !command.isNull()) {
        const auto parsed = playCommand(command.toString());
        if (!parsed) return failed(QStringLiteral("unknown PlayCommand %1").arg(command.toString()));
        request.command = *parsed;
      }
      for (const QJsonValue& id : data.value(QStringLiteral("ItemIds")).toArray()) {
        const QString text = id.toString();
        if (!text.isEmpty()) request.itemIds.append(text);
      }
      if (request.itemIds.isEmpty()) return failed(QStringLiteral("Play without ItemIds"));
      if (const auto start = smallInteger(data.value(QStringLiteral("StartIndex")))) request.startIndex = *start;
      if (request.startIndex < 0 || request.startIndex >= request.itemIds.size())
        return failed(
            QStringLiteral("StartIndex %1 outside the %2 items").arg(request.startIndex).arg(request.itemIds.size()));
      if (const auto ticks = integer(data.value(QStringLiteral("StartPositionTicks"))); ticks && *ticks > 0)
        request.startPositionTicks = *ticks;
      request.mediaSourceId = data.value(QStringLiteral("MediaSourceId")).toString();
      request.audioStreamIndex = smallInteger(data.value(QStringLiteral("AudioStreamIndex")));
      request.subtitleStreamIndex = smallInteger(data.value(QStringLiteral("SubtitleStreamIndex")));
      return ParsedMessage{ServerMessage{request}, {}};
    }

    ParsedMessage parsePlaystate(const QJsonObject& data) {
      const QString name = data.value(QStringLiteral("Command")).toString();
      const auto command = playstateCommand(name);
      if (!command) return failed(QStringLiteral("unknown Playstate command %1").arg(name));
      PlaystateRequest request;
      request.command = *command;
      if (request.command == PlaystateCommand::Seek) {
        request.seekPositionTicks = integer(data.value(QStringLiteral("SeekPositionTicks")));
        if (!request.seekPositionTicks || *request.seekPositionTicks < 0)
          return failed(QStringLiteral("Seek without a position"));
      }
      return ParsedMessage{ServerMessage{request}, {}};
    }

    ParsedMessage parseGeneral(const QJsonObject& data) {
      GeneralCommand command;
      command.name = data.value(QStringLiteral("Name")).toString();
      if (command.name.isEmpty()) return failed(QStringLiteral("GeneralCommand without a name"));
      const QJsonObject arguments = data.value(QStringLiteral("Arguments")).toObject();
      for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const QJsonValue value = it.value();
        if (value.isString())
          command.arguments.insert(it.key(), value.toString());
        else if (value.isDouble())
          command.arguments.insert(it.key(), QString::number(value.toDouble(), 'g', 17));
        else if (value.isBool())
          command.arguments.insert(it.key(), value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
      }
      return ParsedMessage{ServerMessage{command}, {}};
    }
  } // namespace

  ParsedMessage parseServerMessage(const QString& messageType, const QJsonValue& data) {
    const bool play = messageType == QLatin1String("Play");
    const bool playstate = messageType == QLatin1String("Playstate");
    const bool general = messageType == QLatin1String("GeneralCommand");
    if (!play && !playstate && !general) return {};
    if (!data.isObject()) return failed(messageType + QStringLiteral(" without data"));
    const QJsonObject object = data.toObject();
    if (play) return parsePlay(object);
    if (playstate) return parsePlaystate(object);
    return parseGeneral(object);
  }

  const QStringList& knownGeneralCommands() {
    static const QStringList names{
        QStringLiteral("MoveUp"),
        QStringLiteral("MoveDown"),
        QStringLiteral("MoveLeft"),
        QStringLiteral("MoveRight"),
        QStringLiteral("PageUp"),
        QStringLiteral("PageDown"),
        QStringLiteral("PreviousLetter"),
        QStringLiteral("NextLetter"),
        QStringLiteral("ToggleOsd"),
        QStringLiteral("ToggleContextMenu"),
        QStringLiteral("Select"),
        QStringLiteral("Back"),
        QStringLiteral("TakeScreenshot"),
        QStringLiteral("SendKey"),
        QStringLiteral("SendString"),
        QStringLiteral("GoHome"),
        QStringLiteral("GoToSettings"),
        QStringLiteral("VolumeUp"),
        QStringLiteral("VolumeDown"),
        QStringLiteral("Mute"),
        QStringLiteral("Unmute"),
        QStringLiteral("ToggleMute"),
        QStringLiteral("SetVolume"),
        QStringLiteral("SetAudioStreamIndex"),
        QStringLiteral("SetSubtitleStreamIndex"),
        QStringLiteral("ToggleFullscreen"),
        QStringLiteral("DisplayContent"),
        QStringLiteral("GoToSearch"),
        QStringLiteral("DisplayMessage"),
        QStringLiteral("SetRepeatMode"),
        QStringLiteral("ChannelUp"),
        QStringLiteral("ChannelDown"),
        QStringLiteral("Guide"),
        QStringLiteral("ToggleStats"),
        QStringLiteral("PlayMediaSource"),
        QStringLiteral("PlayTrailers"),
        QStringLiteral("SetShuffleQueue"),
        QStringLiteral("PlayState"),
        QStringLiteral("PlayNext"),
        QStringLiteral("ToggleOsdMenu"),
        QStringLiteral("Play"),
        QStringLiteral("SetMaxStreamingBitrate"),
        QStringLiteral("SetPlaybackOrder"),
    };
    return names;
  }

  bool isKnownGeneralCommand(const QString& name) { return knownGeneralCommands().contains(name); }

  std::optional<LocalCommand> parseLocalCommand(const QString& text) {
    const QString command = text.trimmed().toLower();
    using Kind = LocalCommand::Kind;
    if (command == QLatin1String("play-pause")) return LocalCommand{Kind::PlayPause, 0};
    if (command == QLatin1String("next")) return LocalCommand{Kind::Next, 0};
    if (command == QLatin1String("previous")) return LocalCommand{Kind::Previous, 0};
    if (command == QLatin1String("stop")) return LocalCommand{Kind::Stop, 0};
    if (command == QLatin1String("fullscreen")) return LocalCommand{Kind::Fullscreen, 0};
    if (command.startsWith(QLatin1String("seek:"))) {
      const QString value = command.mid(5);
      const bool relative = value.startsWith(QLatin1Char('+')) || value.startsWith(QLatin1Char('-'));
      bool ok = false;
      const double seconds = value.toDouble(&ok);
      // Up to a day either way.
      if (!ok || value.isEmpty() || !std::isfinite(seconds) || std::fabs(seconds) > 86'400.0) return std::nullopt;
      if (!relative && seconds < 0.0) return std::nullopt;
      const auto ms = static_cast<std::int64_t>(std::llround(seconds * 1000.0));
      return LocalCommand{relative ? Kind::SeekBy : Kind::SeekTo, ms};
    }
    return std::nullopt;
  }

  QStringList localCommandNames() {
    return {QStringLiteral("play-pause"), QStringLiteral("next"),      QStringLiteral("previous"),
            QStringLiteral("stop"),       QStringLiteral("seek:+N"),   QStringLiteral("seek:-N"),
            QStringLiteral("seek:N"),     QStringLiteral("fullscreen")};
  }

  std::optional<QString> commandArgument(const QStringList& arguments) {
    const QString option = QStringLiteral("--command");
    // The first entry is the program.
    for (qsizetype i = 1; i < arguments.size(); ++i) {
      const QString& argument = arguments.at(i);
      if (argument == option) return i + 1 < arguments.size() ? arguments.at(i + 1) : QString();
      if (argument.startsWith(option + QLatin1Char('='))) return argument.mid(option.size() + 1);
    }
    return std::nullopt;
  }

} // namespace jfd::remote
