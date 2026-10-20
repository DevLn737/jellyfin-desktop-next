#pragma once

#include <jfd/core/PlaybackSession.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

// Deterministic simulator for the playback session: several clients, each a
// real PlaybackSession with a modelled mpv backend and clock, talking through
// a modelled network to a modelled SyncPlay server. Fake time only; the same
// seed always gives the same run on every platform.
namespace jfd::sim {

  using namespace jfd::core;

  // A small portable PRNG (splitmix64). std distributions differ between
  // standard libraries, which would make seeds platform-specific.
  class Random {
  public:
    explicit Random(std::uint64_t seed) : m_state(seed) {}
    std::uint64_t next() {
      std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      return z ^ (z >> 31);
    }
    // Inclusive range.
    std::int64_t range(std::int64_t low, std::int64_t high) {
      return low + static_cast<std::int64_t>(next() % static_cast<std::uint64_t>(high - low + 1));
    }
    bool chance(int percent) { return range(1, 100) <= percent; }

  private:
    std::uint64_t m_state;
  };

  enum class Mode {
    // Random user actions, faults, reconnects and reorderings; checks invariants.
    Chaos,
    // A clean mixed group with a cold second client; checks drift thresholds.
    Drift,
  };

  struct ScenarioConfig {
    std::uint64_t seed = 1;
    Mode mode = Mode::Chaos;
    int clients = 2;
    std::chrono::seconds duration{180};
    // Group base rate for Drift scenarios.
    PlaybackRate baseRate = PlaybackRate::normal();
    // The server has no playback-rate extension: every rate request is
    // refused with 409 and commands carry no rate.
    bool stockServer = false;
    // Drift: every client adds an external audio track while the group
    // plays (mpv then restarts or waits for data); checks drift after it.
    bool externalAudio = false;
    // Prints the inputs, backend commands and messages of one client.
    int traceClient = -1;
  };

  struct DriftStats {
    std::vector<std::int64_t> samplesMs;
    // Absolute drift of the cold client 10, 15 and 20 s after it joined.
    std::vector<std::int64_t> coldJoinMs;
    // Absolute drift 10 s after a client added external audio.
    std::vector<std::int64_t> afterTrackMs;
    std::int64_t p95() const;
    std::int64_t max() const;
  };

  struct ScenarioResult {
    std::vector<std::string> violations;
    DriftStats drift;
    std::uint64_t inputs = 0;
    std::uint64_t staleInputs = 0;
    std::uint64_t readyReports = 0;
    std::uint64_t corrections = 0;
    std::uint64_t tracksAdded = 0;
  };

  ScenarioResult runScenario(const ScenarioConfig& config);

} // namespace jfd::sim
