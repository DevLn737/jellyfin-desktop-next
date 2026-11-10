#include <jfd/remote/RemoteControl.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/integrations/MediaSessionBridge.h>

#include <QLoggingCategory>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace jfd::remote {

  namespace {
    Q_LOGGING_CATEGORY(lcRemote, "jfd.remote")

    using Outcome = RemoteControl::Outcome;
    using Arguments = QHash<QString, QString>;
    using core::PlaybackPhase;
    using platform::MediaCommand;

    bool active(PlaybackPhase phase) {
      return phase != PlaybackPhase::Idle && phase != PlaybackPhase::Ended && phase != PlaybackPhase::Failed;
    }

    std::optional<qint64> number(const Arguments& arguments, const QString& key) {
      bool ok = false;
      const QString text = arguments.value(key).trimmed();
      const qint64 value = text.toLongLong(&ok);
      if (ok) return value;
      // "40.0" from a sender that wrote a number.
      const double real = text.toDouble(&ok);
      if (ok && std::isfinite(real) && std::trunc(real) == real && std::fabs(real) < 9.0e15)
        return static_cast<qint64>(real);
      return std::nullopt;
    }

    std::optional<int> intArgument(const Arguments& arguments, const QString& key) {
      const auto value = number(arguments, key);
      if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max())
        return std::nullopt;
      return static_cast<int>(*value);
    }

    // One GeneralCommand: the actions it needs, and what it does. Returns
    // false when its arguments are unusable.
    struct GeneralHandler {
      const char* name;
      bool (*available)(const RemoteActions&);
      bool (*run)(const RemoteActions&, const Arguments&);
    };

    bool navigate(const RemoteActions& actions, NavigationKey key) {
      actions.navigate(key);
      return true;
    }

    bool goTo(const RemoteActions& actions, Destination destination) {
      actions.goTo(destination);
      return true;
    }

    bool hasVolume(const RemoteActions& a) { return a.volume && a.setVolume; }
    bool hasMute(const RemoteActions& a) { return a.muted && a.setMuted; }
    bool hasNavigate(const RemoteActions& a) { return bool(a.navigate); }
    bool hasGoTo(const RemoteActions& a) { return bool(a.goTo); }

    // The single list of general commands the client can carry out:
    // SupportedCommands is made from it, and handle() runs through it.
    const std::array<GeneralHandler, 23> GeneralHandlers{{
        {"SetVolume", hasVolume,
         [](const RemoteActions& a, const Arguments& args) {
           const auto volume = intArgument(args, QStringLiteral("Volume"));
           if (!volume) return false;
           a.setVolume(std::clamp(*volume, 0, 100));
           return true;
         }},
        {"VolumeUp", hasVolume,
         [](const RemoteActions& a, const Arguments&) {
           a.setVolume(std::min(100, a.volume() + RemoteControl::VolumeStep));
           return true;
         }},
        {"VolumeDown", hasVolume,
         [](const RemoteActions& a, const Arguments&) {
           a.setVolume(std::max(0, a.volume() - RemoteControl::VolumeStep));
           return true;
         }},
        {"Mute", hasMute,
         [](const RemoteActions& a, const Arguments&) {
           a.setMuted(true);
           return true;
         }},
        {"Unmute", hasMute,
         [](const RemoteActions& a, const Arguments&) {
           a.setMuted(false);
           return true;
         }},
        {"ToggleMute", hasMute,
         [](const RemoteActions& a, const Arguments&) {
           a.setMuted(!a.muted());
           return true;
         }},
        {"SetAudioStreamIndex", [](const RemoteActions& a) { return bool(a.selectAudio); },
         [](const RemoteActions& a, const Arguments& args) {
           const auto index = intArgument(args, QStringLiteral("Index"));
           if (!index || *index < 0) return false;
           a.selectAudio(*index);
           return true;
         }},
        {"SetSubtitleStreamIndex", [](const RemoteActions& a) { return bool(a.selectSubtitle); },
         [](const RemoteActions& a, const Arguments& args) {
           const auto index = intArgument(args, QStringLiteral("Index"));
           if (!index || *index < -1) return false;
           a.selectSubtitle(*index);
           return true;
         }},
        {"DisplayMessage", [](const RemoteActions& a) { return bool(a.showMessage); },
         [](const RemoteActions& a, const Arguments& args) {
           const QString header = args.value(QStringLiteral("Header")).trimmed();
           const QString text = args.value(QStringLiteral("Text")).trimmed();
           if (header.isEmpty() && text.isEmpty()) return false;
           a.showMessage(header, text);
           return true;
         }},
        {"ToggleFullscreen", [](const RemoteActions& a) { return bool(a.toggleFullscreen); },
         [](const RemoteActions& a, const Arguments&) {
           a.toggleFullscreen();
           return true;
         }},
        {"GoHome", hasGoTo, [](const RemoteActions& a, const Arguments&) { return goTo(a, Destination::Home); }},
        {"GoToSearch", hasGoTo, [](const RemoteActions& a, const Arguments&) { return goTo(a, Destination::Search); }},
        {"GoToSettings", hasGoTo,
         [](const RemoteActions& a, const Arguments&) { return goTo(a, Destination::Settings); }},
        {"MoveUp", hasNavigate,
         [](const RemoteActions& a, const Arguments&) { return navigate(a, NavigationKey::Up); }},
        {"MoveDown", hasNavigate,
         [](const RemoteActions& a, const Arguments&) { return navigate(a, NavigationKey::Down); }},
        {"MoveLeft", hasNavigate,
         [](const RemoteActions& a, const Arguments&) { return navigate(a, NavigationKey::Left); }},
        {"MoveRight", hasNavigate,
         [](const RemoteActions& a, const Arguments&) { return navigate(a, NavigationKey::Right); }},
        {"Select", hasNavigate,
         [](const RemoteActions& a, const Arguments&) { return navigate(a, NavigationKey::Select); }},
        {"Back", hasNavigate,
         [](const RemoteActions& a, const Arguments&) { return navigate(a, NavigationKey::Back); }},
        {"SendString", [](const RemoteActions& a) { return bool(a.sendText); },
         [](const RemoteActions& a, const Arguments& args) {
           const QString text = args.value(QStringLiteral("String"));
           if (text.isEmpty()) return false;
           a.sendText(text);
           return true;
         }},
        {"SetRepeatMode", [](const RemoteActions& a) { return bool(a.setRepeatMode); },
         [](const RemoteActions& a, const Arguments& args) {
           const QString mode = args.value(QStringLiteral("RepeatMode"));
           if (mode == QLatin1String("RepeatNone"))
             a.setRepeatMode(RepeatMode::None);
           else if (mode == QLatin1String("RepeatAll"))
             a.setRepeatMode(RepeatMode::All);
           else if (mode == QLatin1String("RepeatOne"))
             a.setRepeatMode(RepeatMode::One);
           else
             return false;
           return true;
         }},
        {"SetShuffleQueue", [](const RemoteActions& a) { return bool(a.setShuffle); },
         [](const RemoteActions& a, const Arguments& args) {
           const QString mode = args.value(QStringLiteral("ShuffleMode"));
           if (mode == QLatin1String("Shuffle"))
             a.setShuffle(true);
           else if (mode == QLatin1String("Sorted"))
             a.setShuffle(false);
           else
             return false;
           return true;
         }},
        {"SetMaxStreamingBitrate", [](const RemoteActions& a) { return bool(a.setMaxBitrate); },
         [](const RemoteActions& a, const Arguments& args) {
           const auto bitrate = number(args, QStringLiteral("MaxBitrate"));
           if (!bitrate || *bitrate < 0) return false;
           a.setMaxBitrate(*bitrate);
           return true;
         }},
    }};

    const GeneralHandler* findHandler(const QString& name) {
      for (const GeneralHandler& handler : GeneralHandlers)
        if (name == QLatin1String(handler.name)) return &handler;
      return nullptr;
    }

    std::optional<MediaCommand> mediaCommand(PlaystateCommand command) {
      switch (command) {
      case PlaystateCommand::Pause: return MediaCommand::Pause;
      case PlaystateCommand::Unpause: return MediaCommand::Play;
      case PlaystateCommand::PlayPause: return MediaCommand::PlayPause;
      case PlaystateCommand::Stop: return MediaCommand::Stop;
      case PlaystateCommand::NextTrack: return MediaCommand::Next;
      case PlaystateCommand::PreviousTrack: return MediaCommand::Previous;
      case PlaystateCommand::Seek:
      case PlaystateCommand::Rewind:
      case PlaystateCommand::FastForward: break;
      }
      return std::nullopt;
    }
  } // namespace

  RemoteControl::RemoteControl(Post post, SnapshotSource snapshot, RemoteActions actions, QObject* parent)
      : QObject(parent), m_post(std::move(post)), m_snapshot(std::move(snapshot)), m_actions(std::move(actions)) {}

  QStringList RemoteControl::supportedCommands() const {
    QStringList names;
    if (!m_enabled) return names;
    for (const GeneralHandler& handler : GeneralHandlers)
      if (handler.available(m_actions)) names.append(QLatin1String(handler.name));
    return names;
  }

  void RemoteControl::applyTo(api::SessionCapabilities& capabilities) const {
    capabilities.supportsMediaControl = m_enabled;
    capabilities.supportedCommands = supportedCommands();
  }

  RemoteControl::Outcome RemoteControl::handle(const QString& messageType, const QJsonValue& data) {
    const ParsedMessage parsed = parseServerMessage(messageType, data);
    if (!parsed.message) {
      if (parsed.error.isEmpty()) return Outcome::NotRemote;
      if (!m_enabled) return Outcome::Disabled;
      qCWarning(lcRemote) << "Remote control message not understood:" << parsed.error;
      return Outcome::Malformed;
    }
    return handle(*parsed.message);
  }

  RemoteControl::Outcome RemoteControl::handle(const ServerMessage& message) {
    if (!m_enabled) {
      qCInfo(lcRemote) << "Remote control is turned off; a command from the server was ignored";
      return Outcome::Disabled;
    }
    if (const auto* request = std::get_if<PlayRequest>(&message)) return play(*request);
    if (const auto* request = std::get_if<PlaystateRequest>(&message)) return playstate(*request);
    return general(std::get<GeneralCommand>(message));
  }

  RemoteControl::Outcome RemoteControl::play(const PlayRequest& request) {
    if (request.command == PlayCommand::PlayInstantMix) {
      qCInfo(lcRemote) << "Ignored Play: instant mixes are not supported";
      return Outcome::Unsupported;
    }
    const bool inGroup = m_snapshot && m_snapshot().group.has_value();
    const auto& action = inGroup ? m_actions.playInGroup : m_actions.play;
    if (!action) {
      qCInfo(lcRemote) << "Ignored Play:" << (inGroup ? "not supported in a group" : "not supported");
      return Outcome::Unsupported;
    }
    qCInfo(lcRemote).nospace() << "Play: " << request.itemIds.size() << " item(s)"
                                << (inGroup ? ", in the group" : "");
    action(request);
    return Outcome::Done;
  }

  RemoteControl::Outcome RemoteControl::playstate(const PlaystateRequest& request) {
    switch (request.command) {
    case PlaystateCommand::Seek: seekTo(*request.seekPositionTicks / core::TicksPerMillisecond); return Outcome::Done;
    case PlaystateCommand::Rewind: seekBy(-RewindMs); return Outcome::Done;
    case PlaystateCommand::FastForward: seekBy(FastForwardMs); return Outcome::Done;
    default: break;
    }
    const auto command = mediaCommand(request.command);
    const PlaybackPhase phase = m_snapshot ? m_snapshot().phase : PlaybackPhase::Idle;
    if (command) {
      if (auto input = integrations::commandInput(*command, phase)) m_post(std::move(*input));
    }
    return Outcome::Done;
  }

  RemoteControl::Outcome RemoteControl::general(const GeneralCommand& command) {
    const GeneralHandler* handler = findHandler(command.name);
    if (handler == nullptr || !handler->available(m_actions)) {
      qCInfo(lcRemote) << "Ignored" << (isKnownGeneralCommand(command.name) ? "unsupported" : "unknown") << "command"
                       << command.name;
      return Outcome::Unsupported;
    }
    if (!handler->run(m_actions, command.arguments)) {
      qCWarning(lcRemote) << "Ignored" << command.name << "with unusable arguments" << command.arguments.keys();
      return Outcome::Malformed;
    }
    return Outcome::Done;
  }

  RemoteControl::Outcome RemoteControl::run(const LocalCommand& command) {
    using Kind = LocalCommand::Kind;
    const PlaybackPhase phase = m_snapshot ? m_snapshot().phase : PlaybackPhase::Idle;
    const auto post = [this, phase](MediaCommand media) {
      if (auto input = integrations::commandInput(media, phase)) m_post(std::move(*input));
    };
    switch (command.kind) {
    case Kind::PlayPause: post(MediaCommand::PlayPause); return Outcome::Done;
    case Kind::Next: post(MediaCommand::Next); return Outcome::Done;
    case Kind::Previous: post(MediaCommand::Previous); return Outcome::Done;
    case Kind::Stop: post(MediaCommand::Stop); return Outcome::Done;
    case Kind::SeekBy: seekBy(command.milliseconds); return Outcome::Done;
    case Kind::SeekTo: seekTo(command.milliseconds); return Outcome::Done;
    case Kind::Fullscreen:
      if (!m_actions.toggleFullscreen) return Outcome::Unsupported;
      m_actions.toggleFullscreen();
      return Outcome::Done;
    }
    return Outcome::Unsupported;
  }

  void RemoteControl::seekTo(std::int64_t milliseconds) {
    // As the media controls do: only while something is open.
    if (!m_snapshot) return;
    const core::SessionSnapshot snapshot = m_snapshot();
    if (!active(snapshot.phase)) return;
    milliseconds = std::max<std::int64_t>(0, milliseconds);
    if (snapshot.duration) milliseconds = std::min(milliseconds, snapshot.duration->milliseconds());
    if (const auto position = core::MediaTicks::fromMilliseconds(milliseconds)) m_post(core::intent::Seek{*position});
  }

  void RemoteControl::seekBy(std::int64_t milliseconds) {
    if (!m_snapshot) return;
    seekTo(m_snapshot().position.milliseconds() + milliseconds);
  }

} // namespace jfd::remote
