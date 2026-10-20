#include "Simulation.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>

namespace jfd::sim {

  namespace {

    using namespace std::chrono_literals;

    template <class... Ts> struct Overloaded : Ts... {
      using Ts::operator()...;
    };
    template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

    WallTicks millis(std::int64_t ms) { return std::chrono::milliseconds(ms); }

    MediaTicks mediaFromSeconds(double seconds) {
      return MediaTicks::fromSeconds(std::max(0.0, seconds)).value_or(MediaTicks{});
    }

    struct World;
    struct Client;

    // ---- Server ------------------------------------------------------------

    struct Member {
      Client* client = nullptr;
      GroupEpoch epoch;
      bool ready = false;
      bool legacy = false;
    };

    struct Server {
      World& world;
      std::vector<Member> members;
      GroupState state = GroupState::Idle;
      bool resume = false;
      PlaylistItemId item{"p1"};
      int itemSerial = 1;
      MediaTicks position;
      ServerInstant anchor;
      PlaybackRate rate = PlaybackRate::normal();
      std::optional<group::Command> lastCommand;
      ServerInstant lastPlayStart;

      explicit Server(World& w) : world(w) {}

      MediaTicks current(ServerInstant now) const {
        if (state != GroupState::Playing || now < anchor) return position;
        return advance(position, mediaElapsed(now - anchor, rate)).value_or(MediaTicks{});
      }
      Member* member(Client* c);
      void join(Client* c, bool legacy);
      void leave(Client* c, bool lost);
      void receive(Client* c, const SyncPlayMessage& message);
      void setState(GroupState next);
      void resolve();
      void startPlaying();
      void freeze();
      void broadcast(GroupCommandType type, WallTicks delay = WallTicks(0));
      void sendCommand(Member& m, GroupCommandType type, ServerInstant when, MediaTicks at);
      void changeItem();
      void resendLast();
      std::vector<std::string> participants() const;
      bool allReady() const {
        return std::all_of(members.begin(), members.end(), [](const Member& m) { return m.ready; });
      }
    };

    // ---- World -------------------------------------------------------------

    struct Event {
      ServerInstant time;
      std::uint64_t sequence;
      std::function<void()> action;
      bool operator>(const Event& other) const {
        return time > other.time || (time == other.time && sequence > other.sequence);
      }
    };

    struct World {
      ScenarioConfig config;
      Random rng;
      ServerInstant now = ServerInstant::fromSinceOrigin(std::chrono::hours(24 * 365 * 56));
      std::uint64_t sequence = 0;
      std::priority_queue<Event, std::vector<Event>, std::greater<>> events;
      std::vector<std::unique_ptr<Client>> clients;
      Server server{*this};
      ScenarioResult result;
      bool chaos() const { return config.mode == Mode::Chaos; }

      explicit World(const ScenarioConfig& c) : config(c), rng(c.seed) {}

      void at(ServerInstant time, std::function<void()> action) {
        events.push(Event{std::max(time, now), ++sequence, std::move(action)});
      }
      void after(WallTicks delay, std::function<void()> action) { at(now + delay, std::move(action)); }
      WallTicks jitter(std::int64_t lowMs, std::int64_t highMs) { return millis(rng.range(lowMs, highMs)); }

      void violation(const std::string& text) {
        if (result.violations.size() < 20) {
          std::ostringstream out;
          out << "seed " << config.seed << " t=" << std::chrono::duration_cast<std::chrono::milliseconds>(now.sinceOrigin()).count() % 10'000'000
              << "ms: " << text;
          result.violations.push_back(out.str());
        } else {
          result.violations.push_back(text);
        }
      }
    };

    // ---- Client ------------------------------------------------------------

    struct File {
      MediaEpoch epoch;
      PlaylistItemId item;
      bool opened = false;
      bool started = false;
      bool paused = false;
      bool seeking = false;
      bool buffering = false;
      double position = 0.0;
      double rate = 1.0;
      ServerInstant updated;
      RequestId latestSeek;
    };

    struct Client final : Clock, Timers, SyncPlayOutbox, SessionObserver, MediaBackend {
      World& world;
      int index;
      WallTicks localOffset;
      WallTicks clockError;
      // The backend's clock runs slightly fast or slow (audio device clock),
      // so positions drift apart unless corrected.
      double speedError;
      std::unique_ptr<PlaybackSession> session;

      // Backend truth.
      std::optional<File> file;
      PlaybackRate appliedRate = PlaybackRate::normal();
      ServerInstant lastBackendDelivery;
      std::uint64_t commandsIssued = 0;

      // Timers.
      std::map<std::uint64_t, TimerId> armed;

      // SyncPlay module.
      EpochCounter<GroupEpochTag> groupEpochs;
      GroupEpoch groupEpoch;
      bool inGroup = false;
      ServerInstant lastSocketDelivery;
      std::uint64_t messagesSent = 0;

      // What the session has been told, for the invariants.
      std::optional<GroupEpoch> sessionGroup;
      PlaybackRate personalRate = PlaybackRate::normal();
      std::optional<ServerInstant> joinedAt;
      bool cold = false;

      Client(World& w, int i) : world(w), index(i) {
        localOffset = millis(w.rng.range(-50'000'000, 50'000'000));
        clockError = w.chaos() ? millis(w.rng.range(-25, 25)) : millis(w.rng.range(-8, 8));
        speedError = static_cast<double>(w.chaos() ? w.rng.range(-5000, 5000) : w.rng.range(-2000, 2000)) / 1e6;
        session = std::make_unique<PlaybackSession>(SessionPorts{*this, *this, *this, *this, *this});
      }

      LocalInstant local(ServerInstant t) const {
        return LocalInstant::fromSinceOrigin(t.sinceOrigin() + localOffset);
      }
      ServerInstant server(LocalInstant t) const {
        return ServerInstant::fromSinceOrigin(t.sinceOrigin() - localOffset);
      }

      // Clock
      LocalInstant now() const override { return local(world.now); }

      // ---- Delivery and the stale-input invariant --------------------------

      bool isStale(const SessionInput& input) const {
        return std::visit(
            Overloaded{
                [&](const intent::Open&) { return false; },
                [&](const intent::Play&) { return false; },
                [&](const intent::Pause&) { return false; },
                [&](const intent::Seek&) { return false; },
                [&](const intent::SetRate&) { return false; },
                [&](const intent::Stop&) { return false; },
                [&](const intent::Next&) { return false; },
                [&](const intent::Previous&) { return false; },
                [&](const group::Joined&) { return false; },
                [&](const group::ClockSynced&) { return false; },
                [&](const TimerFired& fired) { return !armed.contains(fired.id.serial); },
                [&](const auto& event) {
                  using EpochType = std::remove_cvref_t<decltype(event.epoch)>;
                  if constexpr (std::is_same_v<EpochType, MediaEpoch>) {
                    return !file || file->epoch != event.epoch;
                  } else {
                    return !sessionGroup || *sessionGroup != event.epoch;
                  }
                },
            },
            input);
      }

      bool tracing() const { return world.config.traceClient == index; }
      void trace(const std::string& text) const {
        if (!tracing()) return;
        std::printf("%10lld c%d %s\n",
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               world.now.sinceOrigin())
                                               .count() %
                                           100'000'000),
                    index, text.c_str());
      }
      static std::string describe(const SessionInput& input) {
        static const char* names[] = {"Open",      "Play",         "Pause",        "Seek",
                                      "SetRate",   "Stop",         "SourceOpened", "SeekStarted",
                                      "Restarted", "Progress",     "Ended",        "RequestFailed",
                                      "TrackAdded", "Joined",      "Left",         "ConnectionLost",
                                      "State",     "Command",      "Queue",        "RateRejected",
                                      "ClockSynced", "Timer",      "Next",         "Previous",
                                      "SelectTrack", "AddTrack",   "Participants"};
        std::string text = names[input.index()];
        std::visit(Overloaded{
                       [&](const group::Command& c) {
                         text += " type=" + std::to_string(static_cast<int>(c.type)) +
                                 " pos=" + std::to_string(c.position.milliseconds()) +
                                 " rate=" + std::to_string(c.baseRate.perMille());
                       },
                       [&](const backend::PlaybackRestarted& r) {
                         text += " pos=" + std::to_string(r.sample.position ? r.sample.position->value : -1.0) +
                                 " paused=" + std::to_string(r.sample.paused);
                       },
                       [&](const TimerFired& t) {
                         text += " purpose=" + std::to_string(static_cast<int>(t.id.purpose));
                       },
                       [&](const auto&) {},
                   },
                   input);
        return text;
      }

      void deliver(const SessionInput& input) {
        if (tracing() && input.index() != 9 && input.index() != 20) trace("<- " + describe(input));
        ++world.result.inputs;
        const bool stale = isStale(input);
        if (const auto* fired = std::get_if<TimerFired>(&input)) armed.erase(fired->id.serial);
        if (const auto* joined = std::get_if<group::Joined>(&input)) sessionGroup = joined->epoch;
        if (const auto* left = std::get_if<group::Left>(&input); left && !stale) sessionGroup.reset();
        if (stale) {
          ++world.result.staleInputs;
          const SessionSnapshot before = session->snapshot();
          const auto commands = commandsIssued;
          const auto messages = messagesSent;
          session->post(input);
          if (!(session->snapshot() == before) || commands != commandsIssued ||
              messages != messagesSent)
            world.violation("client " + std::to_string(index) +
                            ": a stale input changed state (variant " +
                            std::to_string(input.index()) + ")");
          return;
        }
        session->post(input);
      }

      void postSoon(SessionInput input, WallTicks delay = WallTicks(0)) {
        world.after(delay, [this, input = std::move(input)] { deliver(input); });
      }

      // ---- Backend model ---------------------------------------------------

      void progress() {
        if (!file) return;
        if (file->started && !file->paused && !file->seeking && !file->buffering)
          file->position += std::chrono::duration<double>(world.now - file->updated).count() * file->rate *
                            (1.0 + speedError);
        file->updated = world.now;
      }

      backend::Sample sample() {
        progress();
        return backend::Sample{BackendSeconds{file->position}, file->paused, file->seeking,
                               file->buffering, local(world.now)};
      }

      // mpv events reach the session in order, a little later.
      void emit(SessionInput input) {
        ServerInstant when = world.now + world.jitter(0, 25);
        when = std::max(when, lastBackendDelivery);
        lastBackendDelivery = when;
        world.at(when, [this, input = std::move(input)] { deliver(input); });
      }

      bool checkEpoch(MediaEpoch epoch, const char* command) {
        ++commandsIssued;
        if (!file || file->epoch != epoch) {
          world.violation("client " + std::to_string(index) + ": backend command " + command +
                          " for a file that is not current");
          return false;
        }
        return true;
      }

      MediaEpoch previousMedia;
      void load(MediaEpoch epoch, const BackendLoad& request) override {
        ++commandsIssued;
        if (file) previousMedia = file->epoch;
        if (file && world.chaos() && world.rng.chance(60)) {
          // A late event of the replaced file, as a queued mpv event would be.
          const MediaEpoch old = file->epoch;
          const double oldPosition = file->position;
          const auto kind = world.rng.range(0, 3);
          SessionInput late;
          if (kind == 0)
            late = backend::PlaybackRestarted{
                old, {BackendSeconds{oldPosition}, false, false, false, local(world.now)}};
          else if (kind == 1)
            late = backend::Progress{
                old, {BackendSeconds{oldPosition + 5}, true, false, true, local(world.now)}};
          else if (kind == 2)
            late = backend::Ended{old, backend::EndReason::Error, "late"};
          else
            late = backend::SourceOpened{old, 0.0, 10.0};
          postSoon(late, world.jitter(0, 800));
        }
        File next;
        next.epoch = epoch;
        next.paused = request.startPaused;
        next.updated = world.now;
        next.item = pendingItem;
        file = next;
        const WallTicks latency = cold ? world.jitter(800, 2500) : world.jitter(50, 400);
        world.after(latency, [this, epoch] {
          if (!file || file->epoch != epoch) return;
          file->opened = true;
          emit(backend::SourceOpened{epoch, 0.0, 7200.0});
        });
      }

      void start(MediaEpoch epoch, std::optional<BackendSeconds> position) override {
        if (!checkEpoch(epoch, "start")) return;
        world.after(world.jitter(80, 600), [this, epoch, position] {
          if (!file || file->epoch != epoch) return;
          progress();
          file->position = position ? position->value : 0.0;
          file->started = true;
          emit(backend::PlaybackRestarted{epoch, sample()});
        });
      }

      void seek(MediaEpoch epoch, RequestId request, BackendSeconds target) override {
        trace("   seek " + std::to_string(target.value));
        if (!checkEpoch(epoch, "seek")) return;
        file->latestSeek = request;
        world.after(world.jitter(2, 20), [this, epoch, request, target] {
          if (!file || file->epoch != epoch || file->latestSeek != request) return;
          progress();
          file->seeking = true;
          emit(backend::SeekStarted{epoch});
          world.after(world.jitter(40, world.chaos() ? 600 : 250), [this, epoch, request, target] {
            if (!file || file->epoch != epoch || file->latestSeek != request) return;
            progress();
            if (world.chaos() && file->paused && world.rng.chance(25)) {
              // While paused, a restart may first be reported at the keyframe
              // before the target, then again at the exact position.
              file->position = std::max(0.0, target.value - static_cast<double>(world.rng.range(600, 3000)) / 1000.0);
              file->seeking = false;
              emit(backend::PlaybackRestarted{epoch, sample()});
              world.after(world.jitter(30, 200), [this, epoch, request, target] {
                if (!file || file->epoch != epoch || file->latestSeek != request) return;
                progress();
                file->position = target.value;
                emit(backend::PlaybackRestarted{epoch, sample()});
              });
              return;
            }
            file->position = target.value;
            file->seeking = false;
            emit(backend::PlaybackRestarted{epoch, sample()});
          });
        });
      }

      void setPaused(MediaEpoch epoch, bool paused) override {
        trace(std::string("   setPaused ") + (paused ? "1" : "0"));
        if (!checkEpoch(epoch, "setPaused")) return;
        world.after(world.jitter(0, 30), [this, epoch, paused] {
          if (!file || file->epoch != epoch) return;
          progress();
          file->paused = paused;
          emit(backend::Progress{epoch, sample()});
        });
      }

      void setRate(MediaEpoch epoch, PlaybackRate rate) override {
        if (!checkEpoch(epoch, "setRate")) return;
        appliedRate = rate;
        if (!sessionGroup && rate != personalRate)
          world.violation("client " + std::to_string(index) +
                          ": rate changed outside a group to " + std::to_string(rate.perMille()));
        world.after(world.jitter(0, 30), [this, epoch, rate] {
          if (!file || file->epoch != epoch) return;
          progress();
          file->rate = rate.value();
        });
      }

      // Switching the audio track makes mpv reinitialise audio: it may do a
      // refresh seek at the current position (SEEK, then PLAYBACK_RESTART,
      // losing the time it takes), wait for data briefly, or neither.
      // During a seek mpv repeats that seek instead (issue_refresh_seek), so
      // nothing new is seen.
      void switchTrack(MediaEpoch epoch, TrackKind kind) {
        if (kind != TrackKind::Audio || !file->started || file->seeking) return;
        const auto roll = world.rng.range(1, 10);
        if (roll <= 5) {
          progress();
          file->seeking = true;
          emit(backend::SeekStarted{epoch});
          world.after(world.jitter(30, 300), [this, epoch] {
            if (!file || file->epoch != epoch || !file->seeking) return;
            progress();
            file->seeking = false;
            emit(backend::PlaybackRestarted{epoch, sample()});
          });
        } else if (roll <= 8 && !file->buffering) {
          progress();
          file->buffering = true;
          emit(backend::Progress{epoch, sample()});
          world.after(world.jitter(100, 800), [this, epoch] {
            if (!file || file->epoch != epoch) return;
            progress();
            file->buffering = false;
            emit(backend::Progress{epoch, sample()});
          });
        }
      }

      void selectTrack(MediaEpoch epoch, TrackKind kind, std::optional<int>) override {
        if (!checkEpoch(epoch, "selectTrack")) return;
        world.after(world.jitter(0, 30), [this, epoch, kind] {
          if (file && file->epoch == epoch) switchTrack(epoch, kind);
        });
      }

      int nextTrackId = 10;
      void addExternalTrack(MediaEpoch epoch, RequestId request, const ExternalTrack& track) override {
        if (!checkEpoch(epoch, "addExternalTrack")) return;
        const TrackKind kind = track.kind;
        world.after(world.jitter(100, 1500), [this, epoch, request, kind] {
          if (!file || file->epoch != epoch) return;
          if (world.chaos() && world.rng.chance(10)) {
            emit(backend::RequestFailed{epoch, request, "cannot open"});
            return;
          }
          // Added with "select": the switch follows at once.
          switchTrack(epoch, kind);
          ++world.result.tracksAdded;
          emit(backend::TrackAdded{epoch, request, ++nextTrackId});
        });
      }

      void stop(MediaEpoch epoch) override {
        if (!checkEpoch(epoch, "stop")) return;
        file.reset();
        if (world.chaos() && world.rng.chance(50))
          postSoon(backend::Ended{epoch, backend::EndReason::Stopped, {}}, world.jitter(0, 300));
      }

      void tick() {
        if (file && file->started) {
          if (world.chaos() && !file->buffering && !file->paused && world.rng.chance(1)) {
            file->buffering = true;
            const MediaEpoch epoch = file->epoch;
            world.after(world.jitter(300, 3000), [this, epoch] {
              if (!file || file->epoch != epoch) return;
              progress();
              file->buffering = false;
              emit(backend::Progress{epoch, sample()});
            });
          }
          // mpv also restarts after track switches and cache recovery.
          if (world.chaos() && world.rng.chance(2))
            emit(backend::PlaybackRestarted{file->epoch, sample()});
          emit(backend::Progress{file->epoch, sample()});
        }
        world.after(world.jitter(240, 260), [this] { tick(); });
      }

      // ---- Timers ----------------------------------------------------------

      void arm(TimerId id, LocalInstant due) override {
        armed[id.serial] = id;
        world.at(server(due), [this, id] {
          if (armed.contains(id.serial)) deliver(TimerFired{id});
        });
      }

      void cancel(TimerId id) override {
        armed.erase(id.serial);
        // A timer may fire while its cancellation is still queued.
        if (world.chaos() && world.rng.chance(10)) postSoon(TimerFired{id}, world.jitter(0, 50));
      }

      // ---- SyncPlay module -------------------------------------------------

      void send(const SyncPlayMessage& message) override {
        ++messagesSent;
        trace("-> message " + std::to_string(message.index()) +
              (std::holds_alternative<outgoing::Ready>(message)
                   ? " pos=" + std::to_string(std::get<outgoing::Ready>(message).position.milliseconds())
                   : std::string()));
        std::visit(Overloaded{
                       [&](const outgoing::Ready& ready) { checkReady(ready.position, ready.isPlaying, ready.playlistItem, "Ready"); },
                       [&](const outgoing::Buffering&) {},
                       [&](const auto&) {},
                   },
                   message);
        const GroupEpoch epoch = std::visit([](const auto& m) { return m.epoch; }, message);
        if (!inGroup || epoch != groupEpoch) return;
        // HTTP requests may overtake each other.
        world.after(world.jitter(20, world.chaos() ? 250 : 80), [this, message] {
          world.server.receive(this, message);
        });
      }

      void checkReady(MediaTicks position, bool playing, const PlaylistItemId& item, const char* what) {
        ++world.result.readyReports;
        if (!file || !file->started) {
          world.violation("client " + std::to_string(index) + ": " + what + " without a started file");
          return;
        }
        if (file->item != item) {
          world.violation("client " + std::to_string(index) + ": " + what + " for another queue item");
          return;
        }
        progress();
        const MediaTicks truth = mediaFromSeconds(file->position);
        const auto error = (position - truth).abs();
        const auto tolerance = MediaDelta::fromMilliseconds(playing ? 650 : 500);
        if (error > tolerance)
          world.violation("client " + std::to_string(index) + ": " + what + " position " +
                          std::to_string(position.milliseconds()) + " ms, backend at " +
                          std::to_string(truth.milliseconds()) + " ms");
      }

      void fromServer(SessionInput input) {
        ServerInstant when = world.now + world.jitter(10, world.chaos() ? 150 : 60);
        when = std::max(when, lastSocketDelivery);
        lastSocketDelivery = when;
        world.at(when, [this, input = std::move(input)] { deliver(input); });
      }

      void syncClock() {
        const WallTicks noise = world.chaos() ? world.jitter(-5, 5) : world.jitter(-1, 1);
        deliver(group::ClockSynced{ClockOffset{-localOffset + clockError + noise}, millis(40)});
        world.after(10s, [this] { syncClock(); });
      }

      // ---- Observer and app --------------------------------------------------

      PlaylistItemId pendingItem;

      SyncCorrection lastCorrection = SyncCorrection::None;
      void snapshotChanged(const SessionSnapshot& snapshot) override {
        const SyncCorrection correction =
            snapshot.group ? snapshot.group->correction : SyncCorrection::None;
        if (correction != SyncCorrection::None && correction != lastCorrection) {
          ++world.result.corrections;
          // Leaving during a correction must not let it outlive the group.
          if (world.chaos() && world.rng.chance(30))
            world.after(world.jitter(50, 900), [this] { world.server.leave(this, false); });
        }
        lastCorrection = correction;
      }

      void notify(const SessionNotice& notice) override {
        if (const auto* requested = std::get_if<notice::GroupItemRequested>(&notice)) {
          const auto item = requested->playlistItem;
          const auto position = requested->position;
          const WallTicks delay = cold ? world.jitter(500, 1500) : world.jitter(100, 800);
          world.after(delay, [this, item, position] { openItem(item, position); });
        }
      }

      void openItem(const PlaylistItemId& item, MediaTicks position) {
        MediaRequest request;
        request.item = ItemId("item-" + item.value());
        request.mediaSource = MediaSourceId("source");
        request.playlistItem = item;
        request.url = "sim://" + item.value();
        request.start = position;
        request.runTime = MediaTicks::fromMilliseconds(7'200'000);
        pendingItem = item;
        deliver(intent::Open{request});
      }

      void addAudio(bool stale = false) {
        const MediaEpoch epoch = stale ? previousMedia : session->snapshot().media;
        deliver(intent::AddExternalTrack{
            epoch, {TrackKind::Audio, "sim://audio-" + std::to_string(index), "Dub", "ru"}});
      }

      MediaTicks truePosition() {
        progress();
        return mediaFromSeconds(file ? file->position : 0.0);
      }
    };

    // ---- Server implementation ---------------------------------------------

    std::vector<std::string> Server::participants() const {
      std::vector<std::string> names;
      for (const auto& m : members) names.push_back("user" + std::to_string(m.client->index));
      return names;
    }

    Member* Server::member(Client* c) {
      for (auto& m : members)
        if (m.client == c) return &m;
      return nullptr;
    }

    void Server::setState(GroupState next) {
      state = next;
      for (auto& m : members) m.client->fromServer(group::StateChanged{m.epoch, state});
    }

    void Server::sendCommand(Member& m, GroupCommandType type, ServerInstant when, MediaTicks at) {
      group::Command command;
      command.epoch = m.epoch;
      command.type = type;
      command.playlistItem = item;
      command.when = when;
      command.emittedAt = world.now;
      command.position = at;
      command.baseRate = world.config.stockServer ? PlaybackRate::normal() : rate;
      lastCommand = command;
      m.client->fromServer(command);
    }

    void Server::broadcast(GroupCommandType type, WallTicks delay) {
      for (auto& m : members) sendCommand(m, type, world.now + delay, position);
    }

    void Server::freeze() {
      position = current(world.now);
      anchor = world.now;
    }

    void Server::startPlaying() {
      anchor = world.now + 500ms;
      lastPlayStart = anchor;
      state = GroupState::Playing;
      for (auto& m : members) m.client->fromServer(group::StateChanged{m.epoch, state});
      broadcast(GroupCommandType::Unpause, 500ms);
    }

    void Server::resolve() {
      if (members.empty()) {
        state = GroupState::Idle;
        return;
      }
      if (state != GroupState::Waiting || !allReady()) return;
      if (resume) {
        startPlaying();
      } else {
        setState(GroupState::Paused);
        broadcast(GroupCommandType::Pause);
      }
    }

    void Server::join(Client* c, bool legacy) {
      Member m;
      m.client = c;
      m.legacy = legacy;
      c->groupEpoch = c->groupEpochs.advance();
      c->inGroup = true;
      c->joinedAt = world.now;
      m.epoch = c->groupEpoch;
      const bool wasPlaying = state == GroupState::Playing || (state == GroupState::Waiting && resume);
      freeze();
      members.push_back(m);
      c->fromServer(group::Joined{m.epoch, GroupId("group"), "group", participants()});
      for (auto& other : members)
        if (other.client != c) other.client->fromServer(group::ParticipantsChanged{other.epoch, participants()});
      group::QueueChanged queue;
      queue.epoch = m.epoch;
      queue.reason = QueueChangeReason::NewPlaylist;
      queue.lastUpdate = world.now;
      queue.currentPlaylistItem = item;
      queue.currentItem = ItemId("item-" + item.value());
      queue.startPosition = position;
      queue.isPlaying = wasPlaying;
      queue.baseRate = world.config.stockServer ? PlaybackRate::normal() : rate;
      c->fromServer(queue);
      resume = wasPlaying;
      setState(GroupState::Waiting);
      // Existing members wait paused for the new one, which the server sends
      // to the group's position when the group was playing (as recorded from
      // 12.2: GroupJoined, PlayQueue, then Seek; tests/syncplay/fixtures/recorded).
      for (auto& other : members)
        if (other.client != c) sendCommand(other, GroupCommandType::Pause, world.now, position);
      if (wasPlaying) sendCommand(members.back(), GroupCommandType::Seek, world.now, position);
    }

    void Server::leave(Client* c, bool lost) {
      auto it = std::find_if(members.begin(), members.end(), [c](const Member& m) { return m.client == c; });
      if (it == members.end()) return;
      const GroupEpoch epoch = it->epoch;
      members.erase(it);
      for (auto& other : members)
        other.client->fromServer(group::ParticipantsChanged{other.epoch, participants()});
      if (lost) {
        c->fromServer(group::ConnectionLost{epoch});
      } else {
        c->fromServer(group::Left{epoch});
      }
      c->inGroup = false;
      resolve();
    }

    void Server::changeItem() {
      item = PlaylistItemId("p" + std::to_string(++itemSerial));
      position = MediaTicks{};
      anchor = world.now;
      resume = true;
      for (auto& m : members) {
        m.ready = false;
        group::QueueChanged queue;
        queue.epoch = m.epoch;
        queue.reason = QueueChangeReason::NextItem;
        queue.lastUpdate = world.now;
        queue.currentPlaylistItem = item;
        queue.currentItem = ItemId("item-" + item.value());
        queue.isPlaying = true;
        queue.baseRate = world.config.stockServer ? PlaybackRate::normal() : rate;
        m.client->fromServer(queue);
      }
      setState(GroupState::Waiting);
    }

    void Server::resendLast() {
      if (!lastCommand) return;
      for (auto& m : members) {
        group::Command copy = *lastCommand;
        copy.epoch = m.epoch;
        m.client->fromServer(copy);
      }
    }

    void Server::receive(Client* c, const SyncPlayMessage& message) {
      Member* m = member(c);
      const GroupEpoch epoch = std::visit([](const auto& x) { return x.epoch; }, message);
      if (!m || m->epoch != epoch) return;
      std::visit(
          Overloaded{
              [&](const outgoing::Ready& ready) {
                if (ready.playlistItem != item) return;
                if (state == GroupState::Waiting) {
                  if ((ready.position - position).abs() > ReadyTolerance) {
                    m->ready = false;
                    sendCommand(*m, GroupCommandType::Seek, world.now, position);
                    return;
                  }
                  m->ready = true;
                  resolve();
                } else {
                  m->ready = true;
                }
              },
              [&](const outgoing::Buffering& buffering) {
                if (buffering.playlistItem != item) return;
                m->ready = false;
                if (state == GroupState::Playing) {
                  freeze();
                  resume = true;
                  setState(GroupState::Waiting);
                  broadcast(GroupCommandType::Pause);
                }
              },
              [&](const outgoing::RequestPause&) {
                if (state == GroupState::Playing || state == GroupState::Waiting) {
                  freeze();
                  resume = false;
                  if (state == GroupState::Playing) setState(allReady() ? GroupState::Paused : GroupState::Waiting);
                  broadcast(GroupCommandType::Pause);
                }
              },
              [&](const outgoing::RequestUnpause&) {
                if (state == GroupState::Paused) {
                  if (allReady()) {
                    startPlaying();
                  } else {
                    resume = true;
                    setState(GroupState::Waiting);
                  }
                } else if (state == GroupState::Waiting) {
                  resume = true;
                }
              },
              [&](const outgoing::RequestSeek& seek) {
                resume = state == GroupState::Playing || (state == GroupState::Waiting && resume);
                position = seek.position;
                anchor = world.now;
                for (auto& x : members) x.ready = false;
                setState(GroupState::Waiting);
                broadcast(GroupCommandType::Seek);
              },
              [&](const outgoing::RequestPlaybackRate& request) {
                const bool legacy = std::any_of(members.begin(), members.end(),
                                                [](const Member& x) { return x.legacy; });
                if (world.config.stockServer || (legacy && request.rate != PlaybackRate::normal())) {
                  // Stock 12.2 has no rate endpoint (405); the fork refuses legacy groups (409).
                  c->fromServer(group::RateChangeRejected{m->epoch, world.config.stockServer ? 405 : 409, 1});
                  return;
                }
                resume = state == GroupState::Playing || (state == GroupState::Waiting && resume);
                freeze();
                rate = request.rate;
                for (auto& x : members) x.ready = false;
                setState(GroupState::Waiting);
                broadcast(GroupCommandType::Seek);
              },
              [&](const outgoing::RequestStop&) {},
              [&](const outgoing::RequestNextItem& next) {
                if (next.playlistItem == item) changeItem();
              },
              [&](const outgoing::RequestPreviousItem& previous) {
                if (previous.playlistItem == item) changeItem();
              },
          },
          message);
    }

    // ---- Scenarios -----------------------------------------------------------

    void runUntil(World& world, ServerInstant end) {
      while (!world.events.empty() && world.events.top().time <= end) {
        Event e = world.events.top();
        world.events.pop();
        world.now = e.time;
        e.action();
      }
      world.now = end;
    }

    void chaosAction(World& world) {
      Client& c = *world.clients[static_cast<std::size_t>(
          world.rng.range(0, static_cast<std::int64_t>(world.clients.size()) - 1))];
      const auto roll = world.rng.range(1, 100);
      if (c.inGroup) {
        if (roll <= 18) {
          c.deliver(intent::Pause{});
        } else if (roll <= 40) {
          c.deliver(intent::Play{});
        } else if (roll <= 58) {
          c.deliver(intent::Seek{*MediaTicks::fromMilliseconds(world.rng.range(0, 7'000'000))});
        } else if (roll <= 70) {
          static constexpr std::int32_t rates[] = {1000, 1250, 1500, 750, 2000};
          const auto rate = *PlaybackRate::basePerMille(rates[world.rng.range(0, 4)]);
          // Until the session hears that it joined, the rate is still personal.
          if (!c.sessionGroup) c.personalRate = rate;
          c.deliver(intent::SetRate{rate});
        } else if (roll <= 78) {
          world.after(world.jitter(20, 150), [&world, &c] { world.server.leave(&c, false); });
        } else if (roll <= 84) {
          world.server.leave(&c, true);
          world.after(world.jitter(1000, 5000), [&world, &c] {
            if (!c.inGroup) world.server.join(&c, false);
          });
        } else if (roll <= 90) {
          world.server.resendLast();
        } else if (roll <= 94) {
          world.server.changeItem();
        } else if (roll <= 97) {
          c.addAudio(world.rng.chance(20));
        } else if (roll <= 99) {
          // Sometimes for a file that has since been replaced.
          const MediaEpoch epoch = world.rng.chance(20) ? c.previousMedia : c.session->snapshot().media;
          c.deliver(intent::SelectTrack{epoch, TrackKind::Audio, static_cast<int>(world.rng.range(1, 3))});
        } else {
          c.deliver(intent::Next{});
        }
      } else {
        if (roll <= 30) {
          static constexpr std::int32_t rates[] = {1000, 1250, 1500, 750, 2000};
          const auto rate = *PlaybackRate::basePerMille(rates[world.rng.range(0, 4)]);
          if (!c.sessionGroup) c.personalRate = rate;
          c.deliver(intent::SetRate{rate});
        } else if (roll <= 45) {
          c.deliver(intent::Seek{*MediaTicks::fromMilliseconds(world.rng.range(0, 7'000'000))});
        } else {
          world.after(world.jitter(20, 150), [&world, &c] {
            if (!c.inGroup) world.server.join(&c, false);
          });
        }
      }
    }

    void scheduleChaos(World& world) {
      world.after(world.jitter(500, 8000), [&world] {
        chaosAction(world);
        scheduleChaos(world);
      });
    }

    // Drift of every client that should be in step with a playing group.
    void sampleDrift(World& world) {
      Server& server = world.server;
      const bool settled = server.state == GroupState::Playing && world.now >= server.lastPlayStart + 3s;
      if (settled) {
        for (auto& member : server.members) {
          Client& c = *member.client;
          if (!c.file || !c.file->started || c.file->item != server.item) continue;
          const auto drift = (server.current(world.now) - c.truePosition()).milliseconds();
          world.result.drift.samplesMs.push_back(drift < 0 ? -drift : drift);
        }
      }
      world.after(1s, [&world] { sampleDrift(world); });
    }

  } // namespace

  std::int64_t DriftStats::p95() const {
    if (samplesMs.empty()) return 0;
    std::vector<std::int64_t> sorted = samplesMs;
    std::sort(sorted.begin(), sorted.end());
    const auto index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(sorted.size()))) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
  }

  std::int64_t DriftStats::max() const {
    if (samplesMs.empty()) return 0;
    return *std::max_element(samplesMs.begin(), samplesMs.end());
  }

  ScenarioResult runScenario(const ScenarioConfig& config) {
    World world(config);
    for (int i = 0; i < config.clients; ++i)
      world.clients.push_back(std::make_unique<Client>(world, i));
    for (auto& c : world.clients) {
      c->syncClock();
      c->tick();
    }
    world.server.rate = config.stockServer ? PlaybackRate::normal() : config.baseRate;
    Client& first = *world.clients[0];

    if (config.mode == Mode::Chaos) {
      for (auto& c : world.clients) {
        Client* client = c.get();
        world.after(world.jitter(0, 3000), [&world, client] { world.server.join(client, false); });
      }
      world.after(6s, [&first] { first.deliver(intent::Play{}); });
      scheduleChaos(world);
    } else {
      // One member starts the group; a cold second client joins while it plays.
      world.server.join(&first, false);
      world.after(4s, [&first] { first.deliver(intent::Play{}); });
      for (std::size_t i = 1; i < world.clients.size(); ++i) {
        Client* late = world.clients[i].get();
        late->cold = true;
        world.after(std::chrono::seconds(30 + 20 * static_cast<int>(i - 1)), [&world, late] {
          world.server.join(late, false);
          const ServerInstant joined = world.now;
          for (const auto seconds : {10, 15, 20}) {
            world.at(joined + std::chrono::seconds(seconds), [&world, late, seconds] {
              const bool playing = world.server.state == GroupState::Playing && late->file &&
                                   late->file->started;
              if (!playing) {
                world.violation("cold client not playing " + std::to_string(seconds) +
                                " s after joining");
                return;
              }
              const auto drift = (world.server.current(world.now) - late->truePosition()).milliseconds();
              world.result.drift.coldJoinMs.push_back(drift < 0 ? -drift : drift);
            });
          }
        });
      }
      world.after(5s, [&world] { sampleDrift(world); });

      if (config.externalAudio) {
        // Each client adds a dubbed audio track while the group plays: the
        // first at 60 s, the cold ones 25 s after joining.
        for (std::size_t i = 0; i < world.clients.size(); ++i) {
          Client* client = world.clients[i].get();
          const auto at = i == 0 ? 120s : std::chrono::seconds(30 + 20 * static_cast<int>(i - 1) + 25);
          world.after(at, [&world, client] {
            client->addAudio();
            world.after(10s, [&world, client] {
              const auto& tracks = client->session->snapshot().externalTracks;
              if (tracks.empty() || tracks.back().status != ExternalTrackState::Status::Added)
                world.violation("client " + std::to_string(client->index) +
                                ": external audio not added 10 s after the request");
              if (world.server.state != GroupState::Playing || !client->file || !client->file->started) {
                world.violation("client " + std::to_string(client->index) +
                                " not playing 10 s after adding external audio");
                return;
              }
              const auto drift = (world.server.current(world.now) - client->truePosition()).milliseconds();
              world.result.drift.afterTrackMs.push_back(drift < 0 ? -drift : drift);
            });
          });
        }
      }
    }

    runUntil(world, world.now + config.duration);

    if (config.mode == Mode::Drift) {
      for (const auto value : world.result.drift.coldJoinMs)
        if (value >= 400)
          world.violation("cold client drift " + std::to_string(value) + " ms at a checkpoint");
      for (const auto value : world.result.drift.afterTrackMs)
        if (value >= 400)
          world.violation("drift " + std::to_string(value) + " ms 10 s after adding external audio");
      if (world.result.drift.p95() >= 400)
        world.violation("drift p95 " + std::to_string(world.result.drift.p95()) + " ms");
    }
    return world.result;
  }

} // namespace jfd::sim
