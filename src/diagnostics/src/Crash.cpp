#include <jfd/diagnostics/Crash.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <system_error>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
// After windows.h.
#  include <dbghelp.h>
#  include <csignal>
#else
#  include <csignal>
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__GLIBC__) || defined(__APPLE__)
#    include <execinfo.h>
#    define JFD_HAVE_BACKTRACE 1
#  endif
#endif

namespace jfd::diagnostics {

namespace fs = std::filesystem;

namespace {

bool isCrashFile(const fs::directory_entry& entry)
{
  std::error_code error;
  return entry.is_regular_file(error) && entry.path().filename().string().rfind(CrashFilePrefix, 0) == 0;
}

#ifdef _WIN32

// Set once at install; the handler only reads them.
wchar_t dumpPath[MAX_PATH * 4] = {};
volatile LONG writing = 0;

void writeDump(EXCEPTION_POINTERS* info)
{
  // One dump, from the first thread that gets here.
  if (InterlockedExchange(&writing, 1) != 0)
    return;
  HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;
  MINIDUMP_EXCEPTION_INFORMATION exception = {};
  exception.ThreadId = GetCurrentThreadId();
  exception.ExceptionPointers = info;
  exception.ClientPointers = FALSE;
  // Stacks, threads and modules: enough for a call stack, small (no heap).
  const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
  MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, info != nullptr ? &exception : nullptr,
                    nullptr, nullptr);
  CloseHandle(file);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info)
{
  writeDump(info);
  // Windows ends the process as it would have (and reports it, if set up).
  return EXCEPTION_CONTINUE_SEARCH;
}

// abort(): qFatal, std::terminate (an exception nobody caught), assert.
void onAbort(int)
{
  writeDump(nullptr);
}

#else

// Set once at install; the handler only reads them.
char textPath[4096] = {};
char header[512] = {};
volatile std::sig_atomic_t handling = 0;
// The handler runs here, so a stack overflow can still be reported.
alignas(16) char alternateStack[64 * 1024];
constexpr int CaughtSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

// Only async-signal-safe calls from here on: write, open, close, raise.
void writeAll(int fd, const char* data, std::size_t length)
{
  while (length > 0) {
    const ssize_t written = ::write(fd, data, length);
    if (written <= 0)
      return;
    data += written;
    length -= static_cast<std::size_t>(written);
  }
}

void writeText(int fd, const char* text)
{
  std::size_t length = 0;
  while (text[length] != '\0')
    ++length;
  writeAll(fd, text, length);
}

void writeNumber(int fd, unsigned long long value, unsigned base)
{
  char digits[32];
  std::size_t at = sizeof digits;
  do {
    const auto digit = static_cast<unsigned>(value % base);
    digits[--at] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    value /= base;
  } while (value != 0 && at > 0);
  writeAll(fd, digits + at, sizeof digits - at);
}

const char* signalName(int signal)
{
  switch (signal) {
  case SIGSEGV:
    return "SIGSEGV (invalid memory access)";
  case SIGBUS:
    return "SIGBUS (bus error)";
  case SIGFPE:
    return "SIGFPE (arithmetic error)";
  case SIGILL:
    return "SIGILL (illegal instruction)";
  case SIGABRT:
    return "SIGABRT (aborted)";
  default:
    return "signal";
  }
}

void writeReport(int fd, int signal, const siginfo_t* info, void* const* frames, int frameCount)
{
  writeText(fd, header);
  writeText(fd, "\nSignal: ");
  writeText(fd, signalName(signal));
  writeText(fd, " ");
  writeNumber(fd, static_cast<unsigned long long>(signal), 10);
  if (info != nullptr && signal != SIGABRT) {
    writeText(fd, "\nAddress: 0x");
    writeNumber(fd, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info->si_addr)), 16);
  }
  writeText(fd, "\nBacktrace:\n");
#ifdef JFD_HAVE_BACKTRACE
  backtrace_symbols_fd(frames, frameCount, fd);
#else
  (void)frames;
  (void)frameCount;
  writeText(fd, "(not available)\n");
#endif
}

void onSignal(int signal, siginfo_t* info, void*)
{
  if (handling == 0) {
    handling = 1;
    void* frames[64];
    int frameCount = 0;
#ifdef JFD_HAVE_BACKTRACE
    frameCount = backtrace(frames, 64);
#endif
    const int fd = ::open(textPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
      writeReport(fd, signal, info, frames, frameCount);
      ::close(fd);
    }
    writeReport(STDERR_FILENO, signal, info, frames, frameCount);
  }
  // SA_RESETHAND put the default action back: the process ends as it would
  // have (with a core dump where those are on).
  ::raise(signal);
}

#endif

} // namespace

std::optional<std::string> beginSession(const fs::path& folder, const std::string& info)
{
  std::error_code error;
  fs::create_directories(folder, error);
  const fs::path marker = folder / RunningMarkerName;
  std::optional<std::string> previous;
  if (fs::exists(marker, error)) {
    std::ifstream in(marker, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
      text.pop_back();
    previous = text.empty() ? std::string("unknown") : text;
  }
  std::ofstream out(marker, std::ios::binary | std::ios::trunc);
  out << info << '\n';
  return previous;
}

void endSession(const fs::path& folder)
{
  std::error_code error;
  fs::remove(folder / RunningMarkerName, error);
}

std::vector<fs::path> crashFiles(const fs::path& folder)
{
  std::vector<fs::path> files;
  std::error_code error;
  for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
    if (isCrashFile(*it))
      files.push_back(it->path());
  }
  std::sort(files.begin(), files.end(),
            [](const fs::path& a, const fs::path& b) { return a.filename().string() > b.filename().string(); });
  return files;
}

void pruneCrashFiles(const fs::path& folder, std::size_t keep)
{
  const std::vector<fs::path> files = crashFiles(folder);
  // Newest first, one entry per crash.
  std::set<std::string, std::greater<>> crashes;
  for (const fs::path& file : files)
    crashes.insert(file.stem().string());
  std::set<std::string> kept;
  for (const std::string& crash : crashes) {
    if (kept.size() >= keep)
      break;
    kept.insert(crash);
  }
  std::error_code error;
  for (const fs::path& file : files) {
    if (kept.count(file.stem().string()) == 0)
      fs::remove(file, error);
  }
}

bool installCrashHandler(const fs::path& folder, const std::string& stamp, const std::string& headerText)
{
  std::error_code error;
  fs::create_directories(folder, error);
  const std::string name = std::string(CrashFilePrefix) + stamp;
#ifdef _WIN32
  (void)headerText;
  const std::wstring path = (folder / (name + ".dmp")).wstring();
  if (path.size() + 1 > std::size(dumpPath))
    return false;
  std::copy(path.begin(), path.end(), dumpPath);
  dumpPath[path.size()] = L'\0';
  SetUnhandledExceptionFilter(onUnhandledException);
  std::signal(SIGABRT, onAbort);
  return true;
#else
  const std::string path = (folder / (name + ".txt")).string();
  if (path.size() + 1 > sizeof textPath)
    return false;
  std::memcpy(textPath, path.c_str(), path.size() + 1);
  const std::size_t headerLength = std::min(headerText.size(), sizeof header - 1);
  std::memcpy(header, headerText.data(), headerLength);
  header[headerLength] = '\0';
#  ifdef JFD_HAVE_BACKTRACE
  // The first backtrace() loads the unwinder, which allocates: not in the
  // handler.
  void* frames[4];
  backtrace(frames, 4);
#  endif
  stack_t stack = {};
  stack.ss_sp = alternateStack;
  stack.ss_size = sizeof alternateStack;
  sigaltstack(&stack, nullptr);
  struct sigaction action = {};
  action.sa_sigaction = onSignal;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
  sigemptyset(&action.sa_mask);
  bool ok = true;
  for (const int signal : CaughtSignals)
    ok = sigaction(signal, &action, nullptr) == 0 && ok;
  return ok;
#endif
}

} // namespace jfd::diagnostics
