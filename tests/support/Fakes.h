#pragma once

#include <jfd/core/MediaBackend.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Timers.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Test doubles for the core interfaces, shared by unit tests and the simulator.
namespace jfd::testing {

  using namespace jfd::core;

  class FakeClock final : public Clock {
  public:
    LocalInstant now() const override { return m_now; }
    void set(LocalInstant now) { m_now = now; }
    void advance(WallTicks by) { m_now = m_now + by; }

  private:
    LocalInstant m_now = LocalInstant::fromSinceOrigin(std::chrono::seconds(1000));
  };

  class FakeTimers final : public Timers {
  public:
    void arm(TimerId id, LocalInstant due) override { m_armed[id.serial] = {id, due}; }
    void cancel(TimerId id) override { m_armed.erase(id.serial); }

    // The earliest armed timer, if any.
    std::optional<std::pair<TimerId, LocalInstant>> next() const {
      std::optional<std::pair<TimerId, LocalInstant>> best;
      for (const auto& [serial, entry] : m_armed)
        if (!best || entry.second < best->second) best = entry;
      return best;
    }
    // Removes and returns the timers due at or before `now`, earliest first.
    std::vector<TimerId> takeDue(LocalInstant now) {
      std::vector<std::pair<LocalInstant, TimerId>> due;
      for (auto it = m_armed.begin(); it != m_armed.end();) {
        if (it->second.second <= now) {
          due.emplace_back(it->second.second, it->second.first);
          it = m_armed.erase(it);
        } else {
          ++it;
        }
      }
      std::sort(due.begin(), due.end(), [](const auto& a, const auto& b) {
        return a.first < b.first || (a.first == b.first && a.second.serial < b.second.serial);
      });
      std::vector<TimerId> ids;
      for (const auto& entry : due) ids.push_back(entry.second);
      return ids;
    }
    std::size_t armedCount() const { return m_armed.size(); }
    bool isArmed(TimerPurpose purpose) const {
      for (const auto& [serial, entry] : m_armed)
        if (entry.first.purpose == purpose) return true;
      return false;
    }

  private:
    std::map<std::uint64_t, std::pair<TimerId, LocalInstant>> m_armed;
  };

  namespace command {
    struct Load {
      MediaEpoch epoch;
      BackendLoad request;
    };
    struct Start {
      MediaEpoch epoch;
      std::optional<BackendSeconds> position;
    };
    struct Seek {
      MediaEpoch epoch;
      RequestId request;
      BackendSeconds target;
    };
    struct SetPaused {
      MediaEpoch epoch;
      bool paused;
    };
    struct SetRate {
      MediaEpoch epoch;
      PlaybackRate rate;
    };
    struct SelectTrack {
      MediaEpoch epoch;
      TrackKind kind;
      std::optional<int> trackId;
    };
    struct AddTrack {
      MediaEpoch epoch;
      RequestId request;
      ExternalTrack track;
    };
    struct Stop {
      MediaEpoch epoch;
    };
  } // namespace command

  using BackendCommand =
      std::variant<command::Load, command::Start, command::Seek, command::SetPaused,
                   command::SetRate, command::SelectTrack, command::AddTrack, command::Stop>;

  // Records commands; tests and the simulator decide what the backend reports.
  class RecordingBackend : public MediaBackend {
  public:
    std::vector<BackendCommand> commands;

    void load(MediaEpoch epoch, const BackendLoad& request) override {
      record(command::Load{epoch, request});
    }
    void start(MediaEpoch epoch, std::optional<BackendSeconds> position) override {
      record(command::Start{epoch, position});
    }
    void seek(MediaEpoch epoch, RequestId request, BackendSeconds target) override {
      record(command::Seek{epoch, request, target});
    }
    void setPaused(MediaEpoch epoch, bool paused) override {
      record(command::SetPaused{epoch, paused});
    }
    void setRate(MediaEpoch epoch, PlaybackRate rate) override {
      record(command::SetRate{epoch, rate});
    }
    void selectTrack(MediaEpoch epoch, TrackKind kind, std::optional<int> trackId) override {
      record(command::SelectTrack{epoch, kind, trackId});
    }
    void addExternalTrack(MediaEpoch epoch, RequestId request,
                          const ExternalTrack& track) override {
      record(command::AddTrack{epoch, request, track});
    }
    void stop(MediaEpoch epoch) override { record(command::Stop{epoch}); }

    template <class T> std::vector<T> all() const {
      std::vector<T> result;
      for (const auto& c : commands)
        if (const auto* value = std::get_if<T>(&c)) result.push_back(*value);
      return result;
    }
    template <class T> std::optional<T> last() const {
      for (auto it = commands.rbegin(); it != commands.rend(); ++it)
        if (const auto* value = std::get_if<T>(&*it)) return *value;
      return std::nullopt;
    }

  protected:
    virtual void record(BackendCommand command) { commands.push_back(std::move(command)); }
  };

  class RecordingOutbox final : public SyncPlayOutbox {
  public:
    std::vector<SyncPlayMessage> sent;
    void send(const SyncPlayMessage& message) override { sent.push_back(message); }

    template <class T> std::vector<T> all() const {
      std::vector<T> result;
      for (const auto& m : sent)
        if (const auto* value = std::get_if<T>(&m)) result.push_back(*value);
      return result;
    }
  };

  class RecordingObserver final : public SessionObserver {
  public:
    std::vector<SessionSnapshot> snapshots;
    std::vector<SessionNotice> notices;
    void snapshotChanged(const SessionSnapshot& snapshot) override {
      snapshots.push_back(snapshot);
    }
    void notify(const SessionNotice& notice) override { notices.push_back(notice); }

    template <class T> std::vector<T> all() const {
      std::vector<T> result;
      for (const auto& n : notices)
        if (const auto* value = std::get_if<T>(&n)) result.push_back(*value);
      return result;
    }
  };

} // namespace jfd::testing
