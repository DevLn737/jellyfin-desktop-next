#pragma once

#include <compare>
#include <cstdint>

// Epochs mark which file, group membership or request an input belongs to.
// The playback session drops inputs from an earlier epoch in one place, on
// entry, instead of every component keeping its own generation counter.
namespace jfd::core {

  template <class Tag> class Epoch {
  public:
    constexpr Epoch() = default;
    constexpr std::uint64_t value() const { return m_value; }
    // Epoch 0 is never issued, so a default-constructed epoch is never current.
    constexpr bool isValid() const { return m_value != 0; }
    constexpr auto operator<=>(const Epoch&) const = default;

  private:
    template <class> friend class EpochCounter;
    constexpr explicit Epoch(std::uint64_t value) : m_value(value) {}
    std::uint64_t m_value = 0;
  };

  template <class Tag> class EpochCounter {
  public:
    // Starts a new epoch; everything tagged with an earlier one becomes stale.
    constexpr Epoch<Tag> advance() { return Epoch<Tag>(++m_current); }
    constexpr Epoch<Tag> current() const { return Epoch<Tag>(m_current); }
    constexpr bool isCurrent(Epoch<Tag> epoch) const {
      return epoch.isValid() && epoch.value() == m_current;
    }

  private:
    std::uint64_t m_current = 0;
  };

  struct MediaEpochTag;
  struct GroupEpochTag;
  struct RequestTag;

  // Advances on every file load.
  using MediaEpoch = Epoch<MediaEpochTag>;
  // Advances on SyncPlay group join, leave and reconnect.
  using GroupEpoch = Epoch<GroupEpochTag>;
  // Identifies one seek, rate change or external-audio addition. Request ids
  // are never reused, so a late reply cannot complete a newer request.
  using RequestId = Epoch<RequestTag>;

} // namespace jfd::core
