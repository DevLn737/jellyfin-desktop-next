#include <doctest/doctest.h>
#include <jfd/core/Epoch.h>

using namespace jfd::core;

TEST_CASE("A default epoch is never current") {
  EpochCounter<MediaEpochTag> counter;
  CHECK_FALSE(counter.isCurrent(MediaEpoch{}));
  counter.advance();
  CHECK_FALSE(counter.isCurrent(MediaEpoch{}));
}

TEST_CASE("Advancing makes earlier epochs stale") {
  EpochCounter<MediaEpochTag> counter;
  const auto first = counter.advance();
  CHECK(counter.isCurrent(first));
  const auto second = counter.advance();
  CHECK_FALSE(counter.isCurrent(first));
  CHECK(counter.isCurrent(second));
  CHECK(first < second);
}

TEST_CASE("Request ids are never reused") {
  EpochCounter<RequestTag> requests;
  const auto a = requests.advance();
  const auto b = requests.advance();
  CHECK(a != b);
  CHECK(requests.current() == b);
}
