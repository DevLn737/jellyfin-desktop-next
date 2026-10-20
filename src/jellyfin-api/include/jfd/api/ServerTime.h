#pragma once

#include <jfd/core/Time.h>

#include <QString>

#include <optional>

// Server wall-clock timestamps (SyncPlay When, EmittedAt, LastUpdate,
// GetUtcTime) as core::ServerInstant: 100 ns ticks since the Unix epoch. The
// server writes seven fractional digits; QDateTime keeps only milliseconds,
// which would cost up to 1 ms of clock-offset precision, so these are parsed
// and written directly.
namespace jfd::api {

  // Accepts `YYYY-MM-DDTHH:MM:SS[.fraction](Z|+HH:MM|-HH:MM)`. Digits beyond
  // the seventh fractional digit are truncated. A timestamp without a zone is
  // rejected rather than guessed at.
  std::optional<core::ServerInstant> parseServerInstant(QStringView text);

  // `YYYY-MM-DDTHH:MM:SS.fffffffZ`.
  QString formatServerInstant(core::ServerInstant instant);

  // The system clock as a server instant, for tests and for servers on the
  // same machine. Real code uses TimeSync's ClockOffset instead.
  core::ServerInstant systemClockNow();

} // namespace jfd::api
