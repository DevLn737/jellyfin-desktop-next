#include "FileSync.h"

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <QFile>

#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace jfd::offline {

  bool syncFileToDisk(const QString& path) {
#ifdef Q_OS_WIN
    const std::wstring native = path.toStdWString();
    HANDLE file = CreateFileW(native.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool ok = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    return ok;
#else
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
#endif
  }

} // namespace jfd::offline
