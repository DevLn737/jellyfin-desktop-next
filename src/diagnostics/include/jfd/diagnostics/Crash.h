#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The crash safety net: a "running" marker that tells the next start the
// previous session did not end cleanly, and a handler that leaves a file
// behind when the process dies (Windows: a minidump; Linux and macOS: the
// signal and a backtrace as text). Plain C++, no Qt (ADR 0002).
namespace jfd::diagnostics {

// The marker's file name in the folder given to beginSession().
inline constexpr const char* RunningMarkerName = "running.marker";
// Crash files are named crash-<stamp>.<ext> in the crash folder.
inline constexpr const char* CrashFilePrefix = "crash-";
// How many crashes keep their files (the newest).
inline constexpr std::size_t KeptCrashes = 5;

// Writes the marker into `folder` with `info` (version, start time) in it.
// When a marker was already there, the previous session did not reach
// endSession(): its text is returned (an empty marker gives "unknown").
// Nullopt after a clean exit or on the first start.
std::optional<std::string> beginSession(const std::filesystem::path& folder, const std::string& info);

// The clean exit: removes the marker.
void endSession(const std::filesystem::path& folder);

// The crash files in `folder` (crash-*), newest first: the names start with
// a sortable stamp.
std::vector<std::filesystem::path> crashFiles(const std::filesystem::path& folder);

// Deletes the files of every crash but the newest `keep`. The files of one
// crash share their name up to the extension (crash-<stamp>.dmp,
// crash-<stamp>.log).
void pruneCrashFiles(const std::filesystem::path& folder, std::size_t keep = KeptCrashes);

// Installs the handler for this process. A crash writes
// <folder>/crash-<stamp>.dmp (Windows, MiniDumpWriteDump) or
// <folder>/crash-<stamp>.txt (signal name, fault address and backtrace) and
// lets the process die as it would have. `stamp` is fixed now: nothing is
// formatted or allocated in the handler. `header` is the first line of the
// text file (version). False when it could not be installed.
bool installCrashHandler(const std::filesystem::path& folder, const std::string& stamp,
                         const std::string& header);

} // namespace jfd::diagnostics
