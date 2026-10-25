#pragma once

#include <jfd/core/SessionInput.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Time.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// The last inputs the playback session handled and the SyncPlay messages it
// sent, for the diagnostics report: enough to see what the session heard and
// said when a group went wrong. Entries name epochs, ids, positions and
// flags; never a URL, a token or a file name, so the journal is safe to share.
namespace jfd::core {

  struct JournalEntry {
    LocalInstant at;
    // "in", "dropped" (stale on entry) or "out".
    std::string direction;
    std::string text;
  };

  class SessionJournal {
  public:
    explicit SessionJournal(std::size_t capacity = 500);

    // Records an input as the session saw it. Progress carries only position
    // most of the time; it is recorded when its pause, seeking or buffering
    // flags change, so a long playback does not push out what matters.
    void input(LocalInstant at, const SessionInput& input, bool accepted);
    void output(LocalInstant at, const SyncPlayMessage& message);

    // Oldest first.
    std::vector<JournalEntry> entries() const;
    std::size_t capacity() const { return m_capacity; }
    // Entries recorded in all, including those the ring has dropped since.
    std::uint64_t recorded() const { return m_recorded; }

    // One line per entry: "<seconds> <direction> <text>", local monotonic time.
    std::vector<std::string> lines() const;

  private:
    void add(LocalInstant at, const char* direction, std::string text);

    std::size_t m_capacity;
    std::deque<JournalEntry> m_entries;
    std::uint64_t m_recorded = 0;
    bool m_haveFlags = false;
    bool m_paused = false;
    bool m_seeking = false;
    bool m_buffering = false;
  };

  std::string describe(const SessionInput& input);
  std::string describe(const SyncPlayMessage& message);

} // namespace jfd::core
