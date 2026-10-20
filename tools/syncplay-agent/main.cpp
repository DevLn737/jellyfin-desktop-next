// A headless client built from the real core and syncplay libraries, for
// SyncPlay checks against a live server. It plays a virtual file on the wall
// clock (no decoding), and a driver process does the networking: the agent
// speaks JSON lines on stdin and stdout.
//
// In (one JSON object per line):
//   {"t":"start"}                                start polling server time
//   {"t":"create","name":"..."} {"t":"join","group":"..."} {"t":"leave"}
//   {"t":"intent","op":"play|pause|seek|rate|stop","positionMs":0,"rate":1.0}
//   {"t":"ws","data":"<raw WebSocket message>"}
//   {"t":"connection","state":"lost|restored"}
//   {"t":"reply","id":1,"status":200,"body":"..."}
//   {"t":"sample","id":1}
//   {"t":"quit"}
// Out:
//   {"t":"http","id":1,"method":"GET|POST","path":"...","body":"..."}
//   {"t":"sample","id":1,"at":<unix ms>,"positionMs":0,"playing":false,"rate":1,...}
//   {"t":"notice",...} {"t":"log","text":"..."}
#include <jfd/core/PlaybackSession.h>
#include <jfd/syncplay/GroupClient.h>

#include <nlohmann/json.hpp>

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

using namespace jfd::core;
using namespace std::chrono_literals;
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

  // Scheduled work on one thread, ordered by due time and then by insertion.
  class Loop final : public Clock {
  public:
    LocalInstant now() const override {
      return LocalInstant::fromSinceOrigin(
          std::chrono::duration_cast<WallTicks>(std::chrono::steady_clock::now().time_since_epoch()));
    }

    void at(LocalInstant due, std::function<void()> work) {
      m_queue.emplace(Key{due, m_sequence++}, std::move(work));
    }
    void after(WallTicks delay, std::function<void()> work) { at(now() + delay, std::move(work)); }

    // Runs due work; returns the time until the next item, capped.
    std::chrono::milliseconds runDue() {
      while (!m_queue.empty() && m_queue.begin()->first.due <= now()) {
        auto work = std::move(m_queue.begin()->second);
        m_queue.erase(m_queue.begin());
        work();
      }
      if (m_queue.empty()) return 1000ms;
      const auto wait = std::chrono::ceil<std::chrono::milliseconds>(m_queue.begin()->first.due - now());
      return std::clamp(wait, 0ms, 1000ms);
    }

  private:
    struct Key {
      LocalInstant due;
      std::uint64_t sequence;
      bool operator<(const Key& other) const {
        if (due != other.due) return due < other.due;
        return sequence < other.sequence;
      }
    };
    std::map<Key, std::function<void()>> m_queue;
    std::uint64_t m_sequence = 0;
  };

  struct Options {
    // The virtual file's clock runs this much fast (positive) or slow, as an
    // audio device clock does, so drift correction has work to do.
    double speedErrorPpm = 0.0;
    std::int64_t runTimeMs = 600'000;
    std::int64_t loadLatencyMs = 250;
    std::int64_t seekLatencyMs = 120;
  };

  class Agent final : public MediaBackend, public Timers, public SessionObserver,
                      public jfd::syncplay::Transport {
  public:
    explicit Agent(Options options)
        : m_options(options),
          m_group(*this, m_loop, [this](SessionInput input) { m_session->post(std::move(input)); }) {
      m_session = std::make_unique<PlaybackSession>(SessionPorts{*this, m_loop, *this, m_group, *this});
    }

    int run() {
      tick();
      std::string buffer;
      char chunk[65536];
      while (!m_quit) {
        const auto wait = m_loop.runDue();
        pollfd fd{STDIN_FILENO, POLLIN, 0};
        const int ready = ::poll(&fd, 1, static_cast<int>(wait.count()));
        if (ready < 0) return 1;
        if (ready == 0) continue;
        const ssize_t got = ::read(STDIN_FILENO, chunk, sizeof chunk);
        if (got <= 0) break;
        buffer.append(chunk, static_cast<std::size_t>(got));
        std::size_t newline;
        while ((newline = buffer.find('\n')) != std::string::npos) {
          const std::string line = buffer.substr(0, newline);
          buffer.erase(0, newline + 1);
          if (!line.empty()) handleLine(line);
        }
      }
      return 0;
    }

  private:
    // ---- Driver commands ---------------------------------------------------

    void handleLine(const std::string& line) {
      const json message = json::parse(line, nullptr, false);
      if (message.is_discarded() || !message.is_object()) {
        log("unparsable input");
        return;
      }
      const std::string type = message.value("t", "");
      if (type == "reply") {
        const auto id = message.value("id", std::uint64_t{0});
        const auto found = m_pending.find(id);
        if (found == m_pending.end()) return;
        auto reply = std::move(found->second);
        m_pending.erase(found);
        reply(message.value("status", 0), message.value("body", std::string()));
      } else if (type == "ws") {
        m_group.onMessage(message.value("data", std::string()));
      } else if (type == "sample") {
        emit(sample(message.value("id", std::uint64_t{0})));
      } else if (type == "start") {
        pollTime();
      } else if (type == "create") {
        m_group.createGroup(message.value("name", std::string("agent")));
      } else if (type == "join") {
        m_group.joinGroup(GroupId(message.value("group", std::string())));
      } else if (type == "leave") {
        m_group.leaveGroup();
      } else if (type == "connection") {
        if (message.value("state", "") == "lost")
          m_group.onConnectionLost();
        else
          m_group.onConnectionRestored();
      } else if (type == "intent") {
        intent(message);
      } else if (type == "quit") {
        m_quit = true;
      }
    }

    void intent(const json& message) {
      const std::string op = message.value("op", "");
      if (op == "play") {
        m_session->post(intent::Play{});
      } else if (op == "pause") {
        m_session->post(intent::Pause{});
      } else if (op == "seek") {
        if (const auto position = MediaTicks::fromMilliseconds(message.value("positionMs", std::int64_t{0})))
          m_session->post(intent::Seek{*position});
      } else if (op == "rate") {
        if (const auto rate = PlaybackRate::base(message.value("rate", 1.0)))
          m_session->post(intent::SetRate{*rate});
      } else if (op == "stop") {
        m_session->post(intent::Stop{});
      }
    }

    void pollTime() {
      m_group.pollTime();
      m_loop.after(m_group.nextPollDelay(), [this] { pollTime(); });
    }

    json sample(std::uint64_t id) {
      progress();
      const SessionSnapshot& snapshot = m_session->snapshot();
      json out{{"t", "sample"},
               {"id", id},
               {"at", unixMilliseconds()},
               {"positionMs", nullptr},
               {"playing", false},
               {"rate", 0.0},
               {"baseRate", 1.0},
               {"phase", static_cast<int>(snapshot.phase)},
               {"inGroup", m_group.isInGroup()},
               {"corrections", m_corrections},
               {"skips", m_skips}};
      if (m_file && m_file->started) {
        const bool playing = !m_file->paused && !m_file->seeking;
        out["positionMs"] = m_file->position * 1000.0;
        out["playing"] = playing;
        out["rate"] = playing ? m_file->rate * speedFactor() : 0.0;
      }
      if (snapshot.group) {
        out["baseRate"] = snapshot.group->baseRate.value();
        out["groupState"] = static_cast<int>(snapshot.group->state);
        if (snapshot.group->driftMilliseconds) out["driftMs"] = *snapshot.group->driftMilliseconds;
      }
      return out;
    }

    // ---- Transport -----------------------------------------------------------

    void request(const jfd::syncplay::HttpRequest& request, Reply reply) override {
      const std::uint64_t id = ++m_requestSerial;
      m_pending.emplace(id, std::move(reply));
      emit({{"t", "http"},
            {"id", id},
            {"method", request.method == jfd::syncplay::HttpRequest::Method::Get ? "GET" : "POST"},
            {"path", request.path},
            {"body", request.body}});
    }

    // ---- Virtual file --------------------------------------------------------

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

    void progress() {
      if (!m_file) return;
      const LocalInstant now = m_loop.now();
      if (m_file->started && !m_file->paused && !m_file->seeking) {
        m_file->position += std::chrono::duration<double>(now - m_file->updated).count() *
                            m_file->rate * speedFactor();
        m_file->position = std::min(m_file->position, static_cast<double>(m_options.runTimeMs) / 1000.0);
      }
      m_file->updated = now;
    }

    backend::Sample backendSample() {
      progress();
      return backend::Sample{BackendSeconds{m_file->position}, m_file->paused, m_file->seeking, false,
                             m_loop.now()};
    }

    bool current(MediaEpoch epoch) const { return m_file && m_file->epoch == epoch; }

    // Backend events reach the session a moment later, as mpv's do.
    void later(std::int64_t milliseconds, std::function<void()> work) {
      m_loop.after(std::chrono::milliseconds(milliseconds), std::move(work));
    }

    void load(MediaEpoch epoch, const BackendLoad& request) override {
      File file;
      file.epoch = epoch;
      file.paused = request.startPaused;
      file.updated = m_loop.now();
      m_file = file;
      later(m_options.loadLatencyMs, [this, epoch] {
        if (!current(epoch)) return;
        m_session->post(backend::SourceOpened{epoch, 0.0,
                                              static_cast<double>(m_options.runTimeMs) / 1000.0});
      });
    }

    void start(MediaEpoch epoch, std::optional<BackendSeconds> position) override {
      later(m_options.seekLatencyMs, [this, epoch, position] {
        if (!current(epoch)) return;
        progress();
        m_file->position = position ? position->value : 0.0;
        m_file->started = true;
        m_session->post(backend::PlaybackRestarted{epoch, backendSample()});
      });
    }

    void seek(MediaEpoch epoch, RequestId request, BackendSeconds target) override {
      if (!current(epoch)) return;
      m_file->latestSeek = request;
      later(2, [this, epoch, request, target] {
        if (!current(epoch) || m_file->latestSeek != request) return;
        progress();
        m_file->seeking = true;
        m_session->post(backend::SeekStarted{epoch});
        later(m_options.seekLatencyMs, [this, epoch, request, target] {
          if (!current(epoch) || m_file->latestSeek != request) return;
          progress();
          m_file->position = target.value;
          m_file->seeking = false;
          m_session->post(backend::PlaybackRestarted{epoch, backendSample()});
        });
      });
    }

    void setPaused(MediaEpoch epoch, bool paused) override {
      if (!current(epoch)) return;
      progress();
      m_file->paused = paused;
      later(5, [this, epoch] {
        if (current(epoch)) m_session->post(backend::Progress{epoch, backendSample()});
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
      if (current(epoch)) m_file.reset();
    }

    void tick() {
      if (m_file && m_file->started) m_session->post(backend::Progress{m_file->epoch, backendSample()});
      m_loop.after(250ms, [this] { tick(); });
    }

    // ---- Timers --------------------------------------------------------------

    void arm(TimerId id, LocalInstant due) override {
      m_armed.insert(id.serial);
      m_loop.at(due, [this, id] {
        if (m_armed.erase(id.serial) > 0) m_session->post(TimerFired{id});
      });
    }

    void cancel(TimerId id) override { m_armed.erase(id.serial); }

    // ---- Observer ------------------------------------------------------------

    void snapshotChanged(const SessionSnapshot& snapshot) override {
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

    void notify(const SessionNotice& notice) override {
      std::visit(Overloaded{
                     [&](const notice::GroupItemRequested& requested) {
                       emit({{"t", "notice"},
                             {"kind", "groupItemRequested"},
                             {"item", requested.item.value()},
                             {"positionMs", requested.position.milliseconds()}});
                       open(requested);
                     },
                     [&](const notice::GroupRateRejected& rejected) {
                       emit({{"t", "notice"},
                             {"kind", "groupRateRejected"},
                             {"status", rejected.httpStatus},
                             {"unsupportedParticipants", rejected.unsupportedParticipants}});
                     },
                     [&](const notice::StreamReopenNeeded& reopen) {
                       emit({{"t", "notice"},
                             {"kind", "streamReopenNeeded"},
                             {"positionMs", reopen.position.milliseconds()}});
                     },
                     [&](const notice::ReadyTimedOut& timedOut) {
                       emit({{"t", "notice"},
                             {"kind", "readyTimedOut"},
                             {"positionMs", timedOut.target.milliseconds()}});
                     },
                 },
                 notice);
    }

    // What the app does: resolve the item (here: the virtual file) and open it.
    void open(const notice::GroupItemRequested& requested) {
      MediaRequest media;
      media.item = requested.item;
      media.mediaSource = MediaSourceId(requested.item.value());
      media.playlistItem = requested.playlistItem;
      media.url = "virtual://" + requested.item.value();
      media.start = requested.position;
      media.runTime = MediaTicks::fromMilliseconds(m_options.runTimeMs);
      media.startPaused = true;
      later(50, [this, media] { m_session->post(intent::Open{media}); });
    }

    Options m_options;
    Loop m_loop;
    jfd::syncplay::GroupClient m_group;
    std::unique_ptr<PlaybackSession> m_session;
    std::optional<File> m_file;
    std::set<std::uint64_t> m_armed;
    std::map<std::uint64_t, Reply> m_pending;
    std::uint64_t m_requestSerial = 0;
    SyncCorrection m_lastCorrection = SyncCorrection::None;
    std::uint64_t m_corrections = 0;
    std::uint64_t m_skips = 0;
    bool m_quit = false;
  };

} // namespace

int main(int argc, char** argv) {
  Options options;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string name = argv[i];
    const char* value = argv[i + 1];
    if (name == "--speed-error-ppm")
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
  Agent agent(options);
  return agent.run();
}
