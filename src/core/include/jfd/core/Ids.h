#pragma once

#include <compare>
#include <string>
#include <utility>

// Server-issued identifiers. Distinct types so a playlist occurrence can never
// be passed where an item id is expected: SyncPlay reports name the queue
// occurrence (PlaylistItemId), not the library item.
namespace jfd::core {

  template <class Tag> class StringId {
  public:
    StringId() = default;
    explicit StringId(std::string value) : m_value(std::move(value)) {}

    const std::string& value() const { return m_value; }
    bool isEmpty() const { return m_value.empty(); }

    auto operator<=>(const StringId&) const = default;

  private:
    std::string m_value;
  };

  struct ItemIdTag;
  struct MediaSourceIdTag;
  struct PlaylistItemIdTag;
  struct GroupIdTag;

  using ItemId = StringId<ItemIdTag>;
  using MediaSourceId = StringId<MediaSourceIdTag>;
  // One occurrence of an item in the SyncPlay queue.
  using PlaylistItemId = StringId<PlaylistItemIdTag>;
  using GroupId = StringId<GroupIdTag>;

} // namespace jfd::core
