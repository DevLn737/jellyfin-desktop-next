#pragma once

#include <QString>

namespace jfd::offline {

  // Asks the system to write the file's data to the disk (fsync on POSIX,
  // FlushFileBuffers on Windows) before it is renamed into place, so a power
  // loss right after "complete" does not leave a short file under the final
  // name. False when the file cannot be opened or flushed.
  bool syncFileToDisk(const QString& path);

} // namespace jfd::offline
