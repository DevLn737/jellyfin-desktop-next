#include "Simulation.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// Usage: jfd_simulator [--mode chaos|drift] [--scenarios N] [--seed S]
//                      [--duration SECONDS] [--clients N] [--rate R] [--stock-server]
//                      [--trace CLIENT]
// Exits with 1 when any invariant or threshold is violated.
int main(int argc, char** argv) {
  using namespace jfd::sim;
  ScenarioConfig base;
  int scenarios = 100;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (arg == "--mode") base.mode = value() == "drift" ? Mode::Drift : Mode::Chaos;
    else if (arg == "--scenarios") scenarios = std::stoi(value());
    else if (arg == "--seed") base.seed = std::stoull(value());
    else if (arg == "--duration") base.duration = std::chrono::seconds(std::stoll(value()));
    else if (arg == "--clients") base.clients = std::stoi(value());
    else if (arg == "--rate") base.baseRate = PlaybackRate::base(std::stod(value())).value_or(PlaybackRate::normal());
    else if (arg == "--stock-server") base.stockServer = true;
    else if (arg == "--trace") base.traceClient = std::stoi(value());
    else {
      std::cerr << "unknown argument " << arg << "\n";
      return 2;
    }
  }

  std::uint64_t violations = 0, inputs = 0, stale = 0, ready = 0, corrections = 0;
  std::int64_t worstP95 = 0, worstMax = 0, worstCold = 0;
  for (int n = 0; n < scenarios; ++n) {
    ScenarioConfig config = base;
    config.seed = base.seed + static_cast<std::uint64_t>(n);
    if (config.mode == Mode::Chaos && base.clients == 2) config.clients = 2 + static_cast<int>(config.seed % 3);
    const ScenarioResult result = runScenario(config);
    inputs += result.inputs;
    stale += result.staleInputs;
    ready += result.readyReports;
    corrections += result.corrections;
    worstP95 = std::max(worstP95, result.drift.p95());
    worstMax = std::max(worstMax, result.drift.max());
    for (const auto v : result.drift.coldJoinMs) worstCold = std::max(worstCold, v);
    if (!result.violations.empty()) {
      if (violations < 10)
        for (std::size_t k = 0; k < std::min<std::size_t>(result.violations.size(), 3); ++k)
          std::cout << "VIOLATION " << result.violations[k] << "\n";
      violations += result.violations.size();
    }
  }
  std::cout << "scenarios=" << scenarios << " inputs=" << inputs << " stale_inputs=" << stale
            << " ready_reports=" << ready << " corrections=" << corrections
            << " violations=" << violations;
  if (base.mode == Mode::Drift)
    std::cout << " drift_p95_ms=" << worstP95 << " drift_max_ms=" << worstMax
              << " cold_join_max_ms=" << worstCold;
  std::cout << "\n";
  return violations == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
