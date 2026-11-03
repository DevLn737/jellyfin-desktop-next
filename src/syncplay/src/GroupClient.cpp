#include <jfd/syncplay/GroupClient.h>

#include <algorithm>
#include <utility>

namespace jfd::syncplay {

  namespace {
    template <class... Ts> struct Overloaded : Ts... {
      using Ts::operator()...;
    };
    template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

    bool succeeded(int status) { return status >= 200 && status < 300; }
  } // namespace

  GroupClient::GroupClient(Transport& transport, Clock& clock, InputSink sink, Schedule schedule)
      : m_transport(transport), m_clock(clock), m_sink(std::move(sink)),
        m_schedule(std::move(schedule)) {}

  void GroupClient::later(WallTicks delay, std::function<void()> task) {
    if (!m_schedule) {
      task();
      return;
    }
    m_schedule(delay, [alive = std::weak_ptr<int>(m_alive), task = std::move(task)] {
      if (!alive.expired()) task();
    });
  }

  ServerInstant GroupClient::serverNow() const { return m_offset.toServer(m_clock.now()); }

  // Create and join failures are shown by the group screen (ApiTransport
  // replies); here they go to the session journal.
  void GroupClient::createGroup(std::string_view name) {
    m_transport.request(createGroupRequest(name),
                        [this, alive = std::weak_ptr<int>(m_alive)](int status, std::string) {
                          if (alive.expired() || succeeded(status)) return;
                          m_sink(group::RequestFailed{GroupEpoch{}, group::Request::Create, status});
                        });
  }

  void GroupClient::joinGroup(const GroupId& group) {
    m_desiredGroup = group;
    ++m_rejoinSerial;
    m_transport.request(joinGroupRequest(group),
                        [this, alive = std::weak_ptr<int>(m_alive)](int status, std::string) {
                          if (alive.expired() || succeeded(status)) return;
                          m_sink(group::RequestFailed{GroupEpoch{}, group::Request::Join, status});
                        });
  }

  void GroupClient::leaveGroup() {
    m_desiredGroup.reset();
    ++m_rejoinSerial;
    if (m_inGroup) {
      // The server confirms with GroupLeft. A failed leave ends the
      // membership here: the user asked to leave, and the server drops a
      // member whose session goes.
      const GroupEpoch epoch = m_epoch;
      m_transport.request(leaveGroupRequest(), [this, epoch, alive = std::weak_ptr<int>(m_alive)](
                                                   int status, std::string) {
        if (alive.expired() || succeeded(status) || epoch != m_epoch) return;
        m_sink(group::RequestFailed{epoch, group::Request::Leave, status});
        if (!m_desiredGroup) endMembership();
      });
    } else {
      endMembership();
    }
  }

  void GroupClient::endMembership() {
    if (m_inGroup || m_sessionHoldsGroup) m_sink(group::Left{m_epoch});
    m_inGroup = false;
    m_sessionHoldsGroup = false;
  }

  void GroupClient::onMessage(std::string_view json) {
    if (const auto message = parseMessage(json)) handle(*message);
  }

  void GroupClient::handle(const WireMessage& message) {
    std::visit(
        Overloaded{
            [&](const wire::GroupJoined& joined) {
              ++m_rejoinSerial;
              m_epoch = m_epochs.advance();
              m_inGroup = true;
              m_sessionHoldsGroup = true;
              m_desiredGroup = joined.group;
              m_participants = joined.participants;
              m_sink(group::Joined{m_epoch, joined.group, joined.name, joined.participants});
              if (joined.state) m_sink(group::StateChanged{m_epoch, *joined.state});
            },
            [&](const wire::GroupLeft&) {
              m_desiredGroup.reset();
              endMembership();
            },
            [&](const wire::Refused&) {
              // Rejoining failed (the group is gone or access changed): the
              // membership the session still holds ends here.
              if (!m_inGroup) {
                m_desiredGroup.reset();
                endMembership();
              }
            },
            [&](const wire::Command& command) {
              if (!m_inGroup) return;
              group::Command input;
              input.epoch = m_epoch;
              input.type = command.type;
              input.playlistItem = command.playlistItem;
              // A command without When (an idle Stop) applies now.
              input.when = command.when.value_or(serverNow());
              input.emittedAt = command.emittedAt.value_or(input.when);
              input.position = command.position;
              input.baseRate = command.baseRate;
              m_sink(input);
            },
            [&](const wire::StateUpdate& update) {
              if (m_inGroup) m_sink(group::StateChanged{m_epoch, update.state});
            },
            [&](const wire::PlayQueue& queue) {
              if (!m_inGroup) return;
              group::QueueChanged input;
              input.epoch = m_epoch;
              input.reason = queue.reason;
              // Without LastUpdate the update is taken as the newest.
              input.lastUpdate = queue.lastUpdate.value_or(serverNow());
              input.currentPlaylistItem = queue.currentPlaylistItem;
              input.currentItem = queue.currentItem;
              input.startPosition = queue.startPosition;
              input.isPlaying = queue.isPlaying;
              input.baseRate = queue.baseRate;
              m_sink(input);
            },
            [&](const wire::UserJoined& user) {
              if (!m_inGroup) return;
              m_participants.push_back(user.name);
              m_sink(group::ParticipantsChanged{m_epoch, m_participants});
            },
            [&](const wire::UserLeft& user) {
              if (!m_inGroup) return;
              // One session of that user left; the others stay.
              const auto it = std::find(m_participants.begin(), m_participants.end(), user.name);
              if (it == m_participants.end()) return;
              m_participants.erase(it);
              m_sink(group::ParticipantsChanged{m_epoch, m_participants});
            },
            [&](const wire::Other&) {},
        },
        message);
  }

  void GroupClient::onConnectionLost() {
    m_connectionLost = true;
    ++m_rejoinSerial;
    if (!m_inGroup) return;
    m_inGroup = false;
    m_sink(group::ConnectionLost{m_epoch});
  }

  void GroupClient::onConnectionRestored() {
    m_connectionLost = false;
    // The old offset may be wrong by however long the computer slept: the
    // session waits for a new measurement before using server time.
    m_timeSync.reset();
    m_offset = {};
    m_sink(group::ClockLost{});
    if (m_inGroup) return;
    if (m_desiredGroup) {
      // The lost session's membership is gone on the server; join explicitly.
      ++m_rejoinSerial;
      rejoin(0);
    } else if (m_sessionHoldsGroup) {
      // A leave was in flight when the connection dropped: it is done.
      endMembership();
    }
  }

  void GroupClient::rejoin(int attempt) {
    const std::uint64_t serial = m_rejoinSerial;
    m_transport.request(joinGroupRequest(*m_desiredGroup), [this, serial, attempt,
                                                            alive = std::weak_ptr<int>(m_alive)](
                                                               int status, std::string) {
      // GroupJoined (or Refused) follows a success on the socket.
      if (alive.expired() || serial != m_rejoinSerial || succeeded(status)) return;
      if (m_inGroup || !m_desiredGroup || m_connectionLost) return;
      if (attempt + 1 >= RejoinAttempts) {
        // The user plays on their own, and is told why.
        m_sink(group::RequestFailed{m_epoch, group::Request::Rejoin, status});
        m_desiredGroup.reset();
        endMembership();
        return;
      }
      later(RejoinBackoff * (1 << attempt), [this, serial, attempt] {
        if (serial != m_rejoinSerial || m_inGroup || !m_desiredGroup || m_connectionLost) return;
        rejoin(attempt + 1);
      });
    });
  }

  void GroupClient::pollTime() {
    const LocalInstant sent = m_clock.now();
    m_transport.request(utcTimeRequest(), [this, sent](int status, std::string body) {
      if (status != 200) return;
      const LocalInstant received = m_clock.now();
      const auto reply = parseUtcTimeReply(body);
      if (!reply) return;
      const auto measurement = m_timeSync.record(sent, *reply, received);
      if (!measurement) return;
      m_offset = measurement->offset;
      m_sink(group::ClockSynced{measurement->offset, measurement->roundTrip});
      if (m_inGroup)
        m_transport.request(pingRequest(measurement->roundTrip), [](int, std::string) {});
    });
  }

  void GroupClient::send(const SyncPlayMessage& message) {
    const GroupEpoch epoch = std::visit([](const auto& m) { return m.epoch; }, message);
    if (!m_inGroup || epoch != m_epoch) return;
    if (std::holds_alternative<outgoing::Ready>(message) ||
        std::holds_alternative<outgoing::Buffering>(message)) {
      ++m_reportSerial;
      sendReport(message, false);
      return;
    }
    const bool rateRequest = std::holds_alternative<outgoing::RequestPlaybackRate>(message);
    m_transport.request(requestFor(message), [this, epoch, rateRequest,
                                              alive = std::weak_ptr<int>(m_alive)](int status,
                                                                                   std::string body) {
      if (alive.expired() || !rateRequest || succeeded(status)) return;
      m_sink(group::RateChangeRejected{epoch, status, parseUnsupportedParticipants(body)});
    });
  }

  // The group waits for our Ready: a lost one is sent again once (its When
  // and position still describe the same moment), and a second failure is
  // reported, unless a newer report replaced it meanwhile.
  void GroupClient::sendReport(const SyncPlayMessage& message, bool retried) {
    const GroupEpoch epoch = m_epoch;
    const std::uint64_t serial = m_reportSerial;
    m_transport.request(requestFor(message), [this, message, epoch, serial, retried,
                                              alive = std::weak_ptr<int>(m_alive)](int status,
                                                                                   std::string) {
      if (alive.expired() || succeeded(status)) return;
      if (!m_inGroup || epoch != m_epoch || serial != m_reportSerial) return;
      if (!retried) {
        later(ReadyRetryDelay, [this, message, epoch, serial] {
          if (!m_inGroup || epoch != m_epoch || serial != m_reportSerial) return;
          sendReport(message, true);
        });
        return;
      }
      const auto request = std::holds_alternative<outgoing::Ready>(message)
                               ? group::Request::Ready
                               : group::Request::Buffering;
      m_sink(group::RequestFailed{epoch, request, status});
    });
  }

} // namespace jfd::syncplay
