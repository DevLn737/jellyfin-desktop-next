#pragma once

#include <QString>

#include <atomic>
#include <cstdint>
#include <optional>

// The limits on disk use: "Use at most N GiB for downloads" and "Keep N GiB
// free". A download does not start when its estimate would break either,
// and the downloader asks again every 64 MiB while it writes. The queue
// calls it on the GUI thread, the downloader on its worker thread: limits
// are atomics and the free-space query has no state.
namespace jfd::offline {

  inline constexpr std::int64_t GiB = std::int64_t{1} << 30;
  inline constexpr std::int64_t MiB = std::int64_t{1} << 20;

  enum class StorageVerdict {
    Ok,
    // The downloads would use more than the limit.
    OverLimit,
    // The disk would have less free space than the reserve, or its free
    // space is unknown (a disconnected drive).
    LowSpace,
  };

  class StorageGuard {
  public:
    // Bytes; a limit of 0 means no limit.
    StorageGuard(std::int64_t limitBytes = 0, std::int64_t reserveBytes = 0);
    virtual ~StorageGuard() = default;

    void setLimitBytes(std::int64_t bytes) { m_limit.store(bytes); }
    void setReserveBytes(std::int64_t bytes) { m_reserve.store(bytes); }
    std::int64_t limitBytes() const { return m_limit.load(); }
    std::int64_t reserveBytes() const { return m_reserve.load(); }

    // Estimates carry a 10 % margin (the brief's "×1.1").
    static std::int64_t withMargin(std::int64_t bytes);

    // May a download of `remainingBytes` more start in `folder`, when all
    // downloads (complete, in progress and queued ahead, this one included)
    // are estimated at `totalDownloadBytes`?
    StorageVerdict admit(const QString& folder, std::int64_t remainingBytes,
                         std::int64_t totalDownloadBytes) const;
    // While writing: is there still more free space than the reserve?
    StorageVerdict check(const QString& folder) const;

    // Free bytes for the user on the volume of `folder` (QStorageInfo);
    // nullopt when the volume is not there.
    virtual std::optional<std::int64_t> freeBytes(const QString& folder) const;

  private:
    std::atomic<std::int64_t> m_limit;
    std::atomic<std::int64_t> m_reserve;
  };

} // namespace jfd::offline
