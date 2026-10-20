#include <jfd/syncplay/GroupClient.h>

#include <algorithm>
#include <utility>

namespace jfd::syncplay {

  namespace {
    template <class... Ts> struct Overloaded : Ts... {
      using Ts::operator()...;
    };
    template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;
  } // namespace

  GroupClient::GroupClient(Transport& transport, Clock& clock, InputSink sink)
      : m_transport(transport), m_clock(clock), m_sink(std::move(sink)) {}

  ServerInstant GroupClient::serverNow() const { return m_offset.toServer(m_clock.now()); }

  void GroupClient::createGroup(std::string_view name) {
    m_transport.request(createGroupRequest(name), [](int, std::string) {});
  }

  void GroupClient::joinGroup(const GroupId& group) {
    m_desiredGroup = group;
    m_transport.request(joinGroupRequest(group), [](int, std::string) {});
  }

  void GroupClient::leaveGroup() {
    m_desiredGroup.reset();
    if (m_inGroup) {
      // The server confirms with GroupLeft.
      m_transport.request(leaveGroupRequest(), [](int, std::string) {});
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
    if (!m_inGroup) return;
    m_inGroup = false;
    m_sink(group::ConnectionLost{m_epoch});
  }

  void GroupClient::onConnectionRestored() {
    m_timeSync.reset();
    // The lost session's membership is gone on the server; join explicitly.
    if (m_desiredGroup && !m_inGroup)
      m_transport.request(joinGroupRequest(*m_desiredGroup), [](int, std::string) {});
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
    const bool rateRequest = std::holds_alternative<outgoing::RequestPlaybackRate>(message);
    m_transport.request(requestFor(message), [this, epoch, rateRequest](int status, std::string body) {
      if (!rateRequest || (status >= 200 && status < 300)) return;
      m_sink(group::RateChangeRejected{epoch, status, parseUnsupportedParticipants(body)});
    });
  }

} // namespace jfd::syncplay
