#include "RenderSetup.h"
#include "RepaintRequest.h"

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QQuickItem>

#include <memory>

using jfd::media::RenderContextAttempts;
using jfd::media::RenderSetup;
using jfd::media::RepaintRequest;

// The decisions both video items make on the render thread, without a GPU:
// how often a render context is asked for again, and the repaint request
// mpv's update callback posts.

TEST_CASE("A render context that cannot be created yet is asked for again, then fails") {
  RenderContextAttempts attempts;
  for (int i = 1; i < RenderContextAttempts::Maximum; ++i) REQUIRE(attempts.failed() == RenderSetup::Retry);
  CHECK(attempts.failed() == RenderSetup::Failed);
}

TEST_CASE("A created render context starts the count again for the next window") {
  RenderContextAttempts attempts;
  for (int i = 0; i < 10; ++i) attempts.failed();
  attempts.succeeded();
  CHECK(attempts.count() == 0);
  for (int i = 1; i < RenderContextAttempts::Maximum; ++i) REQUIRE(attempts.failed() == RenderSetup::Retry);
}

TEST_CASE("Repaint requests are coalesced until the GUI thread handles them") {
  auto item = std::make_unique<QQuickItem>();
  RepaintRequest request(item.get());
  request.request();
  CHECK(request.pending());
  request.request();
  QCoreApplication::processEvents();
  CHECK_FALSE(request.pending());
}

TEST_CASE("A repaint queued before the request was deleted runs safely") {
  auto item = std::make_unique<QQuickItem>();
  auto request = std::make_unique<RepaintRequest>(item.get());
  request->request();
  // The node goes away on the render thread with the update still queued.
  request.reset();
  QCoreApplication::processEvents();
  CHECK(item != nullptr);
}

TEST_CASE("A repaint queued before the item was deleted does nothing") {
  auto item = std::make_unique<QQuickItem>();
  RepaintRequest request(item.get());
  request.request();
  item.reset();
  QCoreApplication::processEvents();
  CHECK_FALSE(request.pending());
}
