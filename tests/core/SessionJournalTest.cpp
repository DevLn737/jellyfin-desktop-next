#include "SessionHarness.h"

#include <doctest/doctest.h>

#include <algorithm>

using namespace jfd::core;
using namespace jfd::testing;
using namespace std::chrono_literals;

namespace {
  bool contains(const std::vector<std::string>& lines, const std::string& text) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& line) { return line.find(text) != std::string::npos; });
  }
} // namespace

TEST_CASE("The journal records a group session's inputs and messages, without URLs") {
  SessionHarness h;
  SessionJournal journal;
  h.session->setJournal(&journal);
  h.join("p1");
  MediaRequest request = h.media("p1");
  request.url = "http://server/Videos/item/stream?api_key=secret-token";
  const auto epoch = h.open(request, true);
  h.post(h.command(GroupCommandType::Unpause, 0));
  h.advance(1000ms);
  h.post(backend::Progress{epoch, h.sample(1.0, false, true)});
  h.post(backend::Progress{epoch, h.sample(1.0, false, false)});

  const auto lines = journal.lines();
  CHECK(contains(lines, " in Joined group#1"));
  CHECK(contains(lines, " in Open item item playlist p1 at 0.000s"));
  CHECK(contains(lines, " in Command Unpause group#1 playlist p1 at 0.000s"));
  CHECK(contains(lines, " out Ready group#1 playlist p1 at 0.000s"));
  CHECK(contains(lines, " in Progress media#1 1.000s buffering"));
  CHECK(contains(lines, " out Buffering group#1 playlist p1 at 1.000s"));
  CHECK_FALSE(contains(lines, "secret-token"));
  CHECK_FALSE(contains(lines, "http"));
}

TEST_CASE("Progress is recorded only when its flags change; stale inputs are marked dropped") {
  SessionHarness h;
  SessionJournal journal;
  h.session->setJournal(&journal);
  const auto first = h.open(h.media());
  for (int i = 1; i <= 5; ++i) h.post(backend::Progress{first, h.sample(i, false)});
  h.post(backend::Progress{first, h.sample(6.0, true)});
  h.open(h.media());
  h.post(backend::Progress{first, h.sample(7.0, true)});

  const auto lines = journal.lines();
  const auto progress = std::count_if(lines.begin(), lines.end(), [](const std::string& line) {
    return line.find(" in Progress ") != std::string::npos;
  });
  CHECK(progress == 2);
  CHECK(contains(lines, " dropped Progress media#1 7.000s paused"));
}

TEST_CASE("The journal keeps the last entries up to its capacity") {
  SessionJournal journal(3);
  for (int i = 0; i < 5; ++i)
    journal.input(LocalInstant{}, intent::Seek{*MediaTicks::fromMilliseconds(i * 1000)}, true);
  const auto lines = journal.lines();
  REQUIRE(lines.size() == 3);
  CHECK(lines.front().find("Seek 2.000s") != std::string::npos);
  CHECK(lines.back().find("Seek 4.000s") != std::string::npos);
  CHECK(journal.recorded() == 5);
}
