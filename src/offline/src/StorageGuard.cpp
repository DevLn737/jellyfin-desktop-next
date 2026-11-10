#include <jfd/offline/StorageGuard.h>

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

namespace jfd::offline {

  StorageGuard::StorageGuard(std::int64_t limitBytes, std::int64_t reserveBytes)
      : m_limit(limitBytes), m_reserve(reserveBytes) {}

  std::int64_t StorageGuard::withMargin(std::int64_t bytes) {
    return bytes <= 0 ? 0 : bytes + bytes / 10;
  }

  StorageVerdict StorageGuard::admit(const QString& folder, std::int64_t remainingBytes,
                                     std::int64_t totalDownloadBytes) const {
    const std::int64_t limit = m_limit.load();
    if (limit > 0 && totalDownloadBytes > limit) return StorageVerdict::OverLimit;
    const auto free = freeBytes(folder);
    if (!free || *free - withMargin(remainingBytes) < m_reserve.load())
      return StorageVerdict::LowSpace;
    return StorageVerdict::Ok;
  }

  StorageVerdict StorageGuard::check(const QString& folder) const {
    const auto free = freeBytes(folder);
    if (!free || *free < m_reserve.load()) return StorageVerdict::LowSpace;
    return StorageVerdict::Ok;
  }

  std::optional<std::int64_t> StorageGuard::freeBytes(const QString& folder) const {
    // The folder itself may not exist yet: ask about its nearest existing
    // parent, which is on the same volume.
    QString path = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    while (!path.isEmpty() && !QFileInfo::exists(path)) {
      const QString parent = QFileInfo(path).path();
      if (parent == path) break;
      path = parent;
    }
    QStorageInfo info(path);
    if (!info.isValid() || !info.isReady()) return std::nullopt;
    return info.bytesAvailable();
  }

} // namespace jfd::offline
