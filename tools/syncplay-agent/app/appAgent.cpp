// Our client as the app runs it, without the window, for SyncPlay checks
// that need real playback: headless mpv (no video or audio output), the
// session runtime, the player screen's model (tracks, segments, seeks) and
// the watch-together model. It speaks the protocol of jfd_syncplay_agent
// (../main.cpp), so NativeParticipant drives either, plus:
//
// In:
//   {"t":"watch","item":"<id>","name":"..."}  Watch together on an item page
//   {"t":"tracks","id":1}                     the player's track menus
//   {"t":"audio","index":2} {"t":"subtitle","index":-1}   pick from a menu
//   {"t":"skip"}                              the Skip button of a segment
// Out:
//   {"t":"tracks","id":1,"audio":[...],"subtitle":[...],"audioIndex":1,...}
//   samples also carry "segment", "audioIndex", "subtitleIndex", "item"
//
//   jfd_syncplay_app_agent --server URL --user NAME --password PASS --device ID [--no-rate]
#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/PlayerModel.h>

#include <MpvMediaBackend.h>
#include <MpvPlayer.h>

#include <nlohmann/json.hpp>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTimer>

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

using namespace jfd;
using json = nlohmann::json;

namespace {

  void emit(const json& message) {
    const std::string line = message.dump() + "\n";
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
  }

  void log(const std::string& text) { emit({{"t", "log"}, {"text", text}}); }

  std::int64_t unixMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  json toJson(const QVariant& value) {
    const QByteArray text = QJsonDocument::fromVariant(QVariantList{value}).toJson(QJsonDocument::Compact);
    return json::parse(text.toStdString())[0];
  }

  struct Options {
    QString server;
    QString user;
    QString password;
    QString device = QStringLiteral("jfd-syncplay-app-agent");
    bool supportsRate = true;
  };

  class Agent final : public QObject {
  public:
    explicit Agent(Options options)
        : m_options(std::move(options)),
          m_client(api::ClientIdentity{QStringLiteral("Jellyfin Desktop Next"), QStringLiteral("0.0.1"),
                                       m_options.device, m_options.device}),
          m_socket(m_client), m_mpv([] {
            media::MpvPlayer::Options mpv;
            mpv.audioOutput = QStringLiteral("null");
            mpv.extra = {{"vo", "null"}, {"hr-seek", "yes"}};
            return mpv;
          }()) {
      m_socket.setReconnectDelaysMs(500, 2000);
      m_since.start();
    }

    ~Agent() override {
      m_subscription.reset();
      m_group.reset();
      m_model.reset();
      m_runtime.reset();
    }

    void start() {
      const auto url = api::JellyfinClient::normalizeServerUrl(m_options.server);
      if (!url) return fatal("bad --server");
      m_client.setServerUrl(*url);
      m_client.signIn(m_options.user, m_options.password, this, [this](auto result) {
        if (!result) return fatal("sign-in failed: " + result.error().message.toStdString());
        api::SessionCapabilities capabilities;
        capabilities.supportsSyncPlayPlaybackRate = m_options.supportsRate;
        capabilities.deviceProfile = api::buildDeviceProfile({});
        m_client.reportCapabilities(capabilities, this, [this, capabilities](auto reported) {
          if (!reported) return fatal("capabilities failed: " + reported.error().message.toStdString());
          startRuntime(capabilities.deviceProfile);
        });
      });
    }

  private:
    void fatal(const std::string& text) {
      emit({{"t", "fatal"}, {"text", text}});
      QCoreApplication::exit(1);
    }

    void startRuntime(const QJsonObject& deviceProfile) {
      session::SessionRuntime::Settings settings;
      settings.deviceProfile = deviceProfile;
      m_runtime = std::make_unique<session::SessionRuntime>(
          m_client, m_socket,
          [this](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<media::MpvMediaBackend>(m_mpv, clock, std::move(post));
          },
          std::move(settings));
      // No video item: mpv renders nothing, so the model need not wait for one.
      m_model = std::make_unique<ui::PlayerModel>(*m_runtime, m_client, nullptr);
      m_group = std::make_unique<session::GroupModel>(*m_runtime, m_client, m_socket);

      connect(m_model.get(), &ui::PlayerModel::positionChanged, this, [this] {
        m_position = m_model->position();
        m_positionAt = m_since.elapsed();
      });
      connect(m_model.get(), &ui::PlayerModel::failed, this,
              [](const QString& message) { log("player: " + message.toStdString()); });
      connect(m_group.get(), &session::GroupModel::notice, this, [](const QString& text, bool error) {
        emit({{"t", "notice"}, {"kind", "group"}, {"text", text.toStdString()}, {"error", error}});
      });
      connect(&m_runtime->transport(), &session::ApiTransport::replied, this,
              [](const QString& path, int status) {
                QString bare = path.startsWith(QLatin1Char('/')) ? path.mid(1) : path;
                if (bare == QLatin1String("SyncPlay/Ping") || !bare.startsWith(QLatin1String("SyncPlay/")))
                  return;
                emit({{"t", "request"}, {"path", bare.toStdString()}, {"status", status}});
              });
      connect(&m_socket, &api::SessionSocket::messageReceived, this,
              [](const QString& type, const QJsonValue& data, const QString&) {
                if (type != QLatin1String("SyncPlayCommand") && type != QLatin1String("SyncPlayGroupUpdate"))
                  return;
                const QByteArray text = QJsonDocument(data.toObject()).toJson(QJsonDocument::Compact);
                emit({{"t", "ws"}, {"type", type.toStdString()}, {"data", json::parse(text.toStdString())}});
              });
      connect(&m_socket, &api::SessionSocket::ready, this, [this](quint64 generation) {
        emit({{"t", "ready"}, {"generation", generation}});
        if (generation == 1) listen();
      });
      connect(&m_socket, &api::SessionSocket::disconnected, this,
              [](const QString& reason) { emit({{"t", "disconnected"}, {"reason", reason.toStdString()}}); });
      m_subscription = m_runtime->hub().subscribe(
          [this](const core::SessionSnapshot& snapshot) { onSnapshot(snapshot); },
          [](const core::SessionNotice&) {});
      m_socket.start();
    }

    void listen() {
      m_stdin = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
      connect(m_stdin, &QSocketNotifier::activated, this, [this] {
        char chunk[65536];
        const ssize_t got = ::read(STDIN_FILENO, chunk, sizeof chunk);
        if (got <= 0) {
          m_stdin->setEnabled(false);
          quit();
          return;
        }
        m_buffer.append(chunk, static_cast<std::size_t>(got));
        std::size_t newline;
        while ((newline = m_buffer.find('\n')) != std::string::npos) {
          const std::string line = m_buffer.substr(0, newline);
          m_buffer.erase(0, newline + 1);
          if (!line.empty()) handleLine(line);
        }
      });
    }

    void handleLine(const std::string& line) {
      const json message = json::parse(line, nullptr, false);
      if (message.is_discarded() || !message.is_object()) {
        log("unparsable input");
        return;
      }
      const std::string type = message.value("t", "");
      const auto text = [&](const char* key) { return QString::fromStdString(message.value(key, std::string())); };
      if (type == "sample") {
        emit(sample(message.value("id", std::uint64_t{0})));
      } else if (type == "tracks") {
        emit(tracks(message.value("id", std::uint64_t{0})));
      } else if (type == "create") {
        m_group->createGroup(text("name"));
      } else if (type == "join") {
        m_group->joinGroup(text("group"));
      } else if (type == "leave") {
        m_group->leaveGroup();
      } else if (type == "watch") {
        m_group->watchTogether(text("item"), text("name"), 0);
      } else if (type == "audio") {
        m_model->selectAudio(message.value("index", -1));
      } else if (type == "subtitle") {
        m_model->selectSubtitle(message.value("index", -1));
      } else if (type == "skip") {
        m_model->skipSegment();
      } else if (type == "intent") {
        intent(message);
      } else if (type == "quit") {
        quit();
      }
    }

    // The player screen's controls.
    void intent(const json& message) {
      const std::string op = message.value("op", "");
      if (op == "play" || op == "pause") {
        if (m_model->paused() == (op == "play")) m_model->togglePause();
      } else if (op == "seek") {
        m_model->seek(static_cast<double>(message.value("positionMs", std::int64_t{0})) / 1000.0);
      } else if (op == "rate") {
        m_group->setRate(message.value("rate", 1.0));
      } else if (op == "stop") {
        m_model->stop();
      } else if (op == "next") {
        m_model->next();
      } else if (op == "previous") {
        m_model->previous();
      }
    }

    void quit() {
      if (m_quitting) return;
      m_quitting = true;
      m_runtime->leaveBeforeExit([this] {
        m_socket.stop();
        QCoreApplication::quit();
      });
    }

    json sample(std::uint64_t id) const {
      const core::SessionSnapshot& snapshot = m_runtime->snapshot();
      const bool playing = m_model->active() && !m_model->loading() && !m_model->paused();
      const double rate = playing ? m_mpv.property("speed").toDouble() : 0.0;
      json out{{"t", "sample"},
               {"id", id},
               {"at", unixMilliseconds()},
               {"positionMs", nullptr},
               {"playing", playing},
               {"rate", rate},
               {"baseRate", 1.0},
               {"inGroup", m_runtime->group().isInGroup()},
               {"participants", snapshot.group ? snapshot.group->participants : std::vector<std::string>{}},
               {"corrections", m_corrections},
               {"skips", m_skips},
               {"segment", m_model->segment().toStdString()},
               {"audioIndex", m_model->audioIndex()},
               {"subtitleIndex", m_model->subtitleIndex()},
               {"item", snapshot.item.value()}};
      if (m_model->active() && !m_model->loading()) {
        // The model's position is from the latest progress event; carried
        // on to now at the playing rate.
        const double since = playing ? static_cast<double>(m_since.elapsed() - m_positionAt) / 1000.0 : 0.0;
        out["positionMs"] = (m_position + since * rate) * 1000.0;
      }
      if (snapshot.group) {
        out["baseRate"] = snapshot.group->baseRate.value();
        out["groupState"] = static_cast<int>(snapshot.group->state);
        if (snapshot.group->driftMilliseconds) out["driftMs"] = *snapshot.group->driftMilliseconds;
      }
      return out;
    }

    json tracks(std::uint64_t id) const {
      return {{"t", "tracks"},
              {"id", id},
              {"audio", toJson(m_model->audioTracks())},
              {"subtitle", toJson(m_model->subtitleTracks())},
              {"audioIndex", m_model->audioIndex()},
              {"subtitleIndex", m_model->subtitleIndex()},
              {"mpvAudioExternal", m_mpv.property("current-tracks/audio/external").toBool()},
              {"mpvAudioLang", m_mpv.property("current-tracks/audio/lang").toString().toStdString()},
              {"mpvSubtitleLang", m_mpv.property("current-tracks/sub/lang").toString().toStdString()}};
    }

    void onSnapshot(const core::SessionSnapshot& snapshot) {
      using core::SyncCorrection;
      const SyncCorrection correction = snapshot.group ? snapshot.group->correction : SyncCorrection::None;
      if (correction != m_lastCorrection) {
        if (correction == SyncCorrection::Speed) ++m_corrections;
        if (correction == SyncCorrection::Skip) ++m_skips;
        if (correction != SyncCorrection::None)
          emit({{"t", "correction"},
                {"kind", correction == SyncCorrection::Speed ? "speed" : "skip"},
                {"driftMs", snapshot.group->driftMilliseconds.value_or(0)}});
      }
      m_lastCorrection = correction;
    }

    Options m_options;
    api::JellyfinClient m_client;
    api::SessionSocket m_socket;
    media::MpvPlayer m_mpv;
    std::unique_ptr<session::SessionRuntime> m_runtime;
    std::unique_ptr<ui::PlayerModel> m_model;
    std::unique_ptr<session::GroupModel> m_group;
    session::SessionHub::Subscription m_subscription;
    QSocketNotifier* m_stdin = nullptr;
    std::string m_buffer;
    QElapsedTimer m_since;
    double m_position = 0.0;
    qint64 m_positionAt = 0;
    core::SyncCorrection m_lastCorrection = core::SyncCorrection::None;
    std::uint64_t m_corrections = 0;
    std::uint64_t m_skips = 0;
    bool m_quitting = false;
  };

} // namespace

int main(int argc, char** argv) {
  // No window: the player's model and mpv need a Gui application only.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string name = argv[i];
    if (name == "--no-rate") {
      options.supportsRate = false;
      continue;
    }
    if (i + 1 >= argc) {
      std::fprintf(stderr, "%s needs a value\n", name.c_str());
      return 2;
    }
    const char* value = argv[++i];
    if (name == "--server")
      options.server = QString::fromLocal8Bit(value);
    else if (name == "--user")
      options.user = QString::fromLocal8Bit(value);
    else if (name == "--password")
      options.password = QString::fromLocal8Bit(value);
    else if (name == "--device")
      options.device = QString::fromLocal8Bit(value);
    else if (name == "--speed-error-ppm")
      ; // the virtual file's option; real playback has its own clock
    else {
      std::fprintf(stderr, "unknown option %s\n", name.c_str());
      return 2;
    }
  }
  if (options.server.isEmpty() || options.user.isEmpty()) {
    std::fprintf(stderr, "--server and --user are required\n");
    return 2;
  }
  Agent agent(std::move(options));
  QTimer::singleShot(0, &agent, [&agent] { agent.start(); });
  return app.exec();
}
