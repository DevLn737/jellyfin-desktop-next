// A headless client for SyncPlay checks against a live server, built from
// the app's own session runtime (src/session): the same core, group client,
// API client, session WebSocket and launcher, with a virtual file in place of
// mpv. The file plays on the clock (no decoding) with an adjustable speed
// error, as an audio device clock has, so drift correction has work to do.
// A driver process sends commands as JSON lines on stdin and reads events
// as JSON lines on stdout.
//
//   jfd_syncplay_agent --server URL --user NAME --password PASS --device ID
//                      [--no-rate] [--speed-error-ppm N] [--runtime-ms N]
//                      [--load-latency-ms N] [--seek-latency-ms N]
//
// In (one JSON object per line):
//   {"t":"create","name":"..."} {"t":"join","group":"..."} {"t":"leave"}
//   {"t":"intent","op":"play|pause|seek|rate|stop|next|previous","positionMs":0,"rate":1.0}
//   {"t":"sample","id":1}
//   {"t":"quit"}                        leaves the group as the app does on exit
// Out:
//   {"t":"ready","generation":1}        the session socket is up (again)
//   {"t":"disconnected"}
//   {"t":"resumed","sleptMs":20000}    the computer slept; reconnecting
//   {"t":"ws","type":"SyncPlayCommand|SyncPlayGroupUpdate","data":{...}}
//   {"t":"request","path":"SyncPlay/Join","status":204}
//   {"t":"sample","id":1,"at":<unix ms>,"positionMs":0,"playing":false,"rate":1,...}
//   {"t":"correction",...} {"t":"notice",...} {"t":"log","text":"..."}
//   {"t":"fatal","text":"..."}         then the agent exits
#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/core/MediaBackend.h>
#include <jfd/session/SessionRuntime.h>

#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTimer>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>

using namespace jfd;
using namespace jfd::core;
using json = nlohmann::json;

namespace {

  template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
  };
  template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

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

  struct Options {
    QString server;
    QString user;
    QString password;
    QString device = QStringLiteral("jfd-syncplay-agent");
    bool supportsRate = true;
    // The virtual file's clock runs this much fast (positive) or slow.
    double speedErrorPpm = 0.0;
    std::int64_t runTimeMs = 600'000;
    std::int64_t loadLatencyMs = 250;
    std::int64_t seekLatencyMs = 120;
  };

  // A media backend without decoding. Opens and seeks complete after short
  // delays and progress arrives every 250 ms, as mpv's events do.
  class VirtualFile final : public QObject, public MediaBackend {
  public:
    using Post = session::SessionRuntime::Post;

    VirtualFile(const Clock& clock, Post post, const Options& options)
        : m_clock(clock), m_post(std::move(post)), m_options(options) {
      m_tick.setInterval(250);
      QObject::connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_file && m_file->started) m_post(backend::Progress{m_file->epoch, sample()});
      });
      m_tick.start();
    }

    struct State {
      std::optional<double> positionSeconds;
      bool playing = false;
      double rate = 0.0;
    };

    State state() {
      State out;
      if (!m_file || !m_file->started) return out;
      progress();
      out.positionSeconds = m_file->position;
      out.playing = !m_file->paused && !m_file->seeking;
      out.rate = out.playing ? m_file->rate * speedFactor() : 0.0;
      return out;
    }

    void preload(MediaEpoch, const BackendLoad&) override {}

    void load(MediaEpoch epoch, const BackendLoad& request) override {
      File file;
      file.epoch = epoch;
      file.paused = request.startPaused;
      file.updated = m_clock.now();
      m_file = file;
      later(m_options.loadLatencyMs, [this, epoch] {
        if (!current(epoch)) return;
        m_post(backend::SourceOpened{epoch, 0.0, static_cast<double>(m_options.runTimeMs) / 1000.0});
      });
    }

    void start(MediaEpoch epoch, std::optional<BackendSeconds> position) override {
      later(m_options.seekLatencyMs, [this, epoch, position] {
        if (!current(epoch)) return;
        progress();
        m_file->position = position ? position->value : 0.0;
        m_file->started = true;
        m_post(backend::PlaybackRestarted{epoch, sample()});
      });
    }

    void seek(MediaEpoch epoch, RequestId request, BackendSeconds target) override {
      if (!current(epoch)) return;
      m_file->latestSeek = request;
      later(2, [this, epoch, request, target] {
        if (!current(epoch) || m_file->latestSeek != request) return;
        progress();
        m_file->seeking = true;
        m_post(backend::SeekStarted{epoch});
        later(m_options.seekLatencyMs, [this, epoch, request, target] {
          if (!current(epoch) || m_file->latestSeek != request) return;
          progress();
          m_file->position = target.value;
          m_file->seeking = false;
          m_post(backend::PlaybackRestarted{epoch, sample()});
        });
      });
    }

    void setPaused(MediaEpoch epoch, bool paused) override {
      if (!current(epoch)) return;
      progress();
      m_file->paused = paused;
      later(5, [this, epoch] {
        if (current(epoch)) m_post(backend::Progress{epoch, sample()});
      });
    }

    void setRate(MediaEpoch epoch, PlaybackRate rate) override {
      if (!current(epoch)) return;
      progress();
      m_file->rate = rate.value();
    }

    void selectTrack(MediaEpoch, TrackKind, std::optional<int>) override {}
    void addExternalTrack(MediaEpoch, RequestId, const ExternalTrack&) override {}

    void stop(MediaEpoch epoch) override {
      if (!current(epoch)) return;
      m_file.reset();
      later(5, [this, epoch] { m_post(backend::Ended{epoch, backend::EndReason::Stopped, {}}); });
    }

  private:
    struct File {
      MediaEpoch epoch;
      bool started = false;
      bool paused = true;
      bool seeking = false;
      double position = 0.0;
      double rate = 1.0;
      LocalInstant updated;
      RequestId latestSeek;
    };

    double speedFactor() const { return 1.0 + m_options.speedErrorPpm / 1e6; }
    bool current(MediaEpoch epoch) const { return m_file && m_file->epoch == epoch; }

    void progress() {
      if (!m_file) return;
      const LocalInstant now = m_clock.now();
      if (m_file->started && !m_file->paused && !m_file->seeking) {
        m_file->position +=
            std::chrono::duration<double>(now - m_file->updated).count() * m_file->rate * speedFactor();
        m_file->position = std::min(m_file->position, static_cast<double>(m_options.runTimeMs) / 1000.0);
      }
      m_file->updated = now;
    }

    backend::Sample sample() {
      progress();
      return backend::Sample{BackendSeconds{m_file->position}, m_file->paused, m_file->seeking, false,
                             m_clock.now()};
    }

    // Backend events reach the session a moment later, as mpv's do.
    void later(std::int64_t milliseconds, std::function<void()> work) {
      QTimer::singleShot(std::chrono::milliseconds(milliseconds), Qt::PreciseTimer, this, std::move(work));
    }

    const Clock& m_clock;
    Post m_post;
    Options m_options;
    QTimer m_tick;
    std::optional<File> m_file;
  };

  class Agent final : public QObject {
  public:
    explicit Agent(Options options)
        : m_options(std::move(options)),
          m_client(api::ClientIdentity{QStringLiteral("Jellyfin Desktop Next"), QStringLiteral("0.0.1"),
                                       m_options.device, m_options.device}),
          m_socket(m_client) {
      // A lost network comes back within seconds in the checks; retry often.
      m_socket.setReconnectDelaysMs(500, 2000);
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
      const auto factory = [this](const Clock& clock, session::SessionRuntime::Post post) {
        auto file = std::make_unique<VirtualFile>(clock, std::move(post), m_options);
        m_file = file.get();
        return file;
      };
      m_runtime = std::make_unique<session::SessionRuntime>(m_client, m_socket, factory, settings);

      connect(&m_runtime->transport(), &session::ApiTransport::replied, this,
              [](const QString& path, int status) {
                QString bare = path.startsWith(QLatin1Char('/')) ? path.mid(1) : path;
                if (bare == QLatin1String("SyncPlay/Ping") || !bare.startsWith(QLatin1String("SyncPlay/")))
                  return;
                emit({{"t", "request"}, {"path", bare.toStdString()}, {"status", status}});
              });
      connect(&m_runtime->launcher(), &session::PlaybackLauncher::failed, this,
              [](const QString& itemId, const QString& message) {
                log("could not open " + itemId.toStdString() + ": " + message.toStdString());
              });
      connect(m_runtime.get(), &session::SessionRuntime::queueStepRequested, this,
              [](bool forward) { emit({{"t", "notice"}, {"kind", "queueStep"}, {"forward", forward}}); });
      connect(&m_socket, &api::SessionSocket::messageReceived, this,
              [](const QString& type, const QJsonValue& data, const QString&) {
                if (type != QLatin1String("SyncPlayCommand") && type != QLatin1String("SyncPlayGroupUpdate"))
                  return;
                const QByteArray text = QJsonDocument(data.toObject()).toJson(QJsonDocument::Compact);
                emit({{"t", "ws"}, {"type", type.toStdString()}, {"data", json::parse(text.toStdString())}});
              });
      connect(&m_runtime->sleepDetector(), &session::SleepDetector::resumed, this,
              [](qint64 sleptMs) { emit({{"t", "resumed"}, {"sleptMs", sleptMs}}); });
      connect(&m_socket, &api::SessionSocket::ready, this,
              [this](quint64 generation) {
                emit({{"t", "ready"}, {"generation", generation}});
                if (generation == 1) listen();
              });
      connect(&m_socket, &api::SessionSocket::disconnected, this,
              [](const QString& reason) { emit({{"t", "disconnected"}, {"reason", reason.toStdString()}}); });
      m_subscription = m_runtime->hub().subscribe(
          [this](const SessionSnapshot& snapshot) { onSnapshot(snapshot); },
          [](const SessionNotice& notice) { onNotice(notice); });
      m_socket.start();
    }

    // ---- Driver commands ---------------------------------------------------

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
      if (type == "sample") {
        emit(sample(message.value("id", std::uint64_t{0})));
      } else if (type == "create") {
        m_runtime->createGroup(QString::fromStdString(message.value("name", std::string("agent"))));
      } else if (type == "join") {
        m_runtime->joinGroup(QString::fromStdString(message.value("group", std::string())));
      } else if (type == "leave") {
        m_runtime->leaveGroup();
      } else if (type == "intent") {
        intent(message);
      } else if (type == "quit") {
        quit();
      }
    }

    void intent(const json& message) {
      const std::string op = message.value("op", "");
      if (op == "play") {
        m_runtime->post(intent::Play{});
      } else if (op == "pause") {
        m_runtime->post(intent::Pause{});
      } else if (op == "seek") {
        if (const auto position = MediaTicks::fromMilliseconds(message.value("positionMs", std::int64_t{0})))
          m_runtime->post(intent::Seek{*position});
      } else if (op == "rate") {
        if (const auto rate = PlaybackRate::base(message.value("rate", 1.0)))
          m_runtime->post(intent::SetRate{*rate});
      } else if (op == "stop") {
        m_runtime->post(intent::Stop{});
      } else if (op == "next") {
        m_runtime->post(intent::Next{});
      } else if (op == "previous") {
        m_runtime->post(intent::Previous{});
      }
    }

    // As the app does on exit: leave the group, then stop the socket.
    void quit() {
      if (m_quitting) return;
      m_quitting = true;
      m_runtime->leaveBeforeExit([this] {
        m_socket.stop();
        QCoreApplication::quit();
      });
    }

    json sample(std::uint64_t id) const {
      const SessionSnapshot& snapshot = m_runtime->snapshot();
      json out{{"t", "sample"},
               {"id", id},
               {"at", unixMilliseconds()},
               {"positionMs", nullptr},
               {"playing", false},
               {"rate", 0.0},
               {"baseRate", 1.0},
               {"phase", static_cast<int>(snapshot.phase)},
               {"inGroup", m_runtime->group().isInGroup()},
               {"participants", snapshot.group ? snapshot.group->participants : std::vector<std::string>{}},
               {"corrections", m_corrections},
               {"skips", m_skips}};
      const VirtualFile::State file = m_file->state();
      if (file.positionSeconds) {
        out["positionMs"] = *file.positionSeconds * 1000.0;
        out["playing"] = file.playing;
        out["rate"] = file.rate;
      }
      if (snapshot.group) {
        out["baseRate"] = snapshot.group->baseRate.value();
        out["groupState"] = static_cast<int>(snapshot.group->state);
        if (snapshot.group->driftMilliseconds) out["driftMs"] = *snapshot.group->driftMilliseconds;
      }
      return out;
    }

    // ---- Session state -------------------------------------------------------

    void onSnapshot(const SessionSnapshot& snapshot) {
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

    static void onNotice(const SessionNotice& notice) {
      std::visit(Overloaded{
                     [](const notice::GroupItemRequested& requested) {
                       emit({{"t", "notice"},
                             {"kind", "groupItemRequested"},
                             {"item", requested.item.value()},
                             {"positionMs", requested.position.milliseconds()}});
                     },
                     [](const notice::GroupRateRejected& rejected) {
                       emit({{"t", "notice"},
                             {"kind", "groupRateRejected"},
                             {"status", rejected.httpStatus},
                             {"unsupportedParticipants", rejected.unsupportedParticipants}});
                     },
                     [](const notice::StreamReopenNeeded& reopen) {
                       emit({{"t", "notice"},
                             {"kind", "streamReopenNeeded"},
                             {"positionMs", reopen.position.milliseconds()}});
                     },
                     [](const notice::ReadyTimedOut& timedOut) {
                       emit({{"t", "notice"},
                             {"kind", "readyTimedOut"},
                             {"positionMs", timedOut.target.milliseconds()}});
                     },
                     // Reported by the runtime's queueStepRequested signal.
                     [](const notice::QueueStepRequested&) {},
                 },
                 notice);
    }

    Options m_options;
    api::JellyfinClient m_client;
    api::SessionSocket m_socket;
    std::unique_ptr<session::SessionRuntime> m_runtime;
    session::SessionHub::Subscription m_subscription;
    VirtualFile* m_file = nullptr;
    QSocketNotifier* m_stdin = nullptr;
    std::string m_buffer;
    SyncCorrection m_lastCorrection = SyncCorrection::None;
    std::uint64_t m_corrections = 0;
    std::uint64_t m_skips = 0;
    bool m_quitting = false;
  };

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
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
      options.speedErrorPpm = std::strtod(value, nullptr);
    else if (name == "--runtime-ms")
      options.runTimeMs = std::strtoll(value, nullptr, 10);
    else if (name == "--load-latency-ms")
      options.loadLatencyMs = std::strtoll(value, nullptr, 10);
    else if (name == "--seek-latency-ms")
      options.seekLatencyMs = std::strtoll(value, nullptr, 10);
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
