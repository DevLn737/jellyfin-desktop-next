#pragma once

#include <mpv/client.h>

#include <memory>

namespace jfd::media {

// Shared ownership of an mpv core. The render context holds a copy, so the
// core is destroyed only after the render context has been freed.
using MpvHandle = std::shared_ptr<mpv_handle>;

inline MpvHandle createMpv()
{
  return MpvHandle(mpv_create(), [](mpv_handle* handle) {
    if (handle)
      mpv_terminate_destroy(handle);
  });
}

}
