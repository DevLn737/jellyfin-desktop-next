#pragma once

#include <jfd/api/Models.h>
#include <jfd/integrations/MediaSessionBridge.h>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::integrations {

  // The details of a library item for the media controls and Discord. For an
  // episode the poster is its series'. The queue flags are left false.
  NowPlayingDetails nowPlayingDetails(const api::Item& item, const api::JellyfinClient& client);

  // The item whose poster stands for this one: an episode's series, a
  // track's album, else the item itself.
  QString posterItemId(const api::Item& item);

} // namespace jfd::integrations
