#include <jfd/syncplay/Protocol.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>

namespace jfd::syncplay {

  namespace {

    using Json = nlohmann::json;

    // Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant).
    std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
      y -= m <= 2;
      const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
      const auto yoe = static_cast<unsigned>(y - era * 400);
      const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
      const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
    }

    void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
      z += 719468;
      const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
      const auto doe = static_cast<unsigned>(z - era * 146097);
      const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
      y = static_cast<std::int64_t>(yoe) + era * 400;
      const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
      const unsigned mp = (5 * doy + 2) / 153;
      d = doy - (153 * mp + 2) / 5 + 1;
      m = mp < 10 ? mp + 3 : mp - 9;
      y += m <= 2;
    }

    bool digits(std::string_view text, std::size_t at, std::size_t count, int& value) {
      if (at + count > text.size()) return false;
      value = 0;
      for (std::size_t i = at; i < at + count; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10 + (text[i] - '0');
      }
      return true;
    }

    const Json* field(const Json& object, const char* name) {
      if (!object.is_object()) return nullptr;
      const auto it = object.find(name);
      if (it == object.end() || it->is_null()) return nullptr;
      return &*it;
    }

    std::string stringField(const Json& object, const char* name) {
      const Json* value = field(object, name);
      return value && value->is_string() ? value->get<std::string>() : std::string();
    }

    std::optional<ServerInstant> timeField(const Json& object, const char* name) {
      const Json* value = field(object, name);
      if (!value || !value->is_string()) return std::nullopt;
      return parseServerTime(value->get<std::string>());
    }

    std::optional<std::int64_t> integerField(const Json& object, const char* name) {
      const Json* value = field(object, name);
      if (!value || !value->is_number_integer()) return std::nullopt;
      return value->get<std::int64_t>();
    }

    // Missing, null, non-numeric or out-of-range means 1x (contract).
    PlaybackRate rateField(const Json& object, const char* name) {
      const Json* value = field(object, name);
      if (!value || !value->is_number()) return PlaybackRate::normal();
      return PlaybackRate::base(value->get<double>()).value_or(PlaybackRate::normal());
    }

    std::optional<GroupState> parseState(const std::string& text) {
      if (text == "Idle") return GroupState::Idle;
      if (text == "Waiting") return GroupState::Waiting;
      if (text == "Paused") return GroupState::Paused;
      if (text == "Playing") return GroupState::Playing;
      return std::nullopt;
    }

    QueueChangeReason parseQueueReason(const std::string& text) {
      static const std::pair<const char*, QueueChangeReason> reasons[] = {
          {"NewPlaylist", QueueChangeReason::NewPlaylist},
          {"SetCurrentItem", QueueChangeReason::SetCurrentItem},
          {"NextItem", QueueChangeReason::NextItem},
          {"PreviousItem", QueueChangeReason::PreviousItem},
          {"RemoveItems", QueueChangeReason::RemoveItems},
          {"MoveItem", QueueChangeReason::MoveItem},
          {"Queue", QueueChangeReason::Queue},
          {"QueueNext", QueueChangeReason::QueueNext},
          {"RepeatMode", QueueChangeReason::RepeatMode},
          {"ShuffleMode", QueueChangeReason::ShuffleMode},
      };
      for (const auto& [name, reason] : reasons)
        if (text == name) return reason;
      return QueueChangeReason::Unknown;
    }

    std::optional<WireMessage> parseCommand(const Json& data) {
      wire::Command command;
      const std::string type = stringField(data, "Command");
      if (type == "Unpause") command.type = GroupCommandType::Unpause;
      else if (type == "Pause") command.type = GroupCommandType::Pause;
      else if (type == "Stop") command.type = GroupCommandType::Stop;
      else if (type == "Seek") command.type = GroupCommandType::Seek;
      else return std::nullopt;

      command.groupId = stringField(data, "GroupId");
      command.playlistItem = PlaylistItemId(stringField(data, "PlaylistItemId"));
      command.when = timeField(data, "When");
      command.emittedAt = timeField(data, "EmittedAt");
      command.baseRate = rateField(data, "PlaybackRate");
      const auto ticks = integerField(data, "PositionTicks");
      if (ticks) {
        const auto position = MediaTicks::fromTicks(*ticks);
        if (!position) return std::nullopt;
        command.position = *position;
      } else if (command.type != GroupCommandType::Stop) {
        return std::nullopt;
      }
      return command;
    }

    std::optional<WireMessage> parsePlayQueue(const Json& data) {
      wire::PlayQueue queue;
      queue.reason = parseQueueReason(stringField(data, "Reason"));
      queue.lastUpdate = timeField(data, "LastUpdate");
      queue.isPlaying = field(data, "IsPlaying") && data["IsPlaying"].is_boolean() &&
                        data["IsPlaying"].get<bool>();
      queue.baseRate = rateField(data, "PlaybackRate");
      if (const auto start = integerField(data, "StartPositionTicks"))
        queue.startPosition = MediaTicks::fromTicks(*start).value_or(MediaTicks{});
      const auto index = integerField(data, "PlayingItemIndex").value_or(-1);
      const Json* playlist = field(data, "Playlist");
      if (playlist && playlist->is_array() && index >= 0 &&
          index < static_cast<std::int64_t>(playlist->size())) {
        const Json& entry = (*playlist)[static_cast<std::size_t>(index)];
        queue.currentPlaylistItem = PlaylistItemId(stringField(entry, "PlaylistItemId"));
        queue.currentItem = ItemId(stringField(entry, "ItemId"));
      }
      return queue;
    }

    std::optional<WireMessage> parseGroupUpdate(const Json& update) {
      const std::string type = stringField(update, "Type");
      const Json empty = Json::object();
      const Json* inner = field(update, "Data");
      const Json& data = inner ? *inner : empty;
      if (type == "GroupJoined") {
        wire::GroupJoined joined;
        std::string id = stringField(update, "GroupId");
        if (id.empty()) id = stringField(data, "GroupId");
        joined.group = GroupId(id);
        joined.state = parseState(stringField(data, "State"));
        joined.name = stringField(data, "GroupName");
        if (const Json* participants = field(data, "Participants"); participants && participants->is_array())
          for (const Json& participant : *participants)
            if (participant.is_string()) joined.participants.push_back(participant.get<std::string>());
        return joined;
      }
      if (type == "UserJoined" || type == "UserLeft") {
        const std::string name = inner && inner->is_string() ? inner->get<std::string>() : std::string();
        if (type == "UserJoined") return wire::UserJoined{name};
        return wire::UserLeft{name};
      }
      if (type == "GroupLeft") return wire::GroupLeft{};
      if (type == "StateUpdate") {
        const auto state = parseState(stringField(data, "State"));
        if (!state) return std::nullopt;
        return wire::StateUpdate{*state, stringField(data, "Reason")};
      }
      if (type == "PlayQueue") return parsePlayQueue(data);
      if (type == "NotInGroup" || type == "GroupDoesNotExist" || type == "CreateGroupDenied" ||
          type == "JoinGroupDenied" || type == "LibraryAccessDenied")
        return wire::Refused{type};
      return wire::Other{"SyncPlayGroupUpdate/" + type};
    }

    std::string itemBody(const PlaylistItemId& item) {
      Json body = Json::object();
      body["PlaylistItemId"] = item.value();
      return body.dump();
    }

    std::string positionBody(const char* name, MediaTicks position) {
      Json body = Json::object();
      body[name] = position.ticks();
      return body.dump();
    }

    template <class Report> std::string reportBody(const Report& report) {
      Json body = Json::object();
      body["When"] = formatServerTime(report.when);
      body["PositionTicks"] = report.position.ticks();
      body["IsPlaying"] = report.isPlaying;
      body["PlaylistItemId"] = report.playlistItem.value();
      return body.dump();
    }

  } // namespace

  std::optional<ServerInstant> parseServerTime(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SS[.fffffff](Z|+HH:MM|-HH:MM)
    int year, month, day, hour, minute, second;
    if (!digits(text, 0, 4, year) || text.size() < 19 || text[4] != '-' ||
        !digits(text, 5, 2, month) || text[7] != '-' || !digits(text, 8, 2, day) ||
        (text[10] != 'T' && text[10] != ' ') || !digits(text, 11, 2, hour) || text[13] != ':' ||
        !digits(text, 14, 2, minute) || text[16] != ':' || !digits(text, 17, 2, second))
      return std::nullopt;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
      return std::nullopt;

    std::size_t at = 19;
    std::int64_t fraction = 0;
    if (at < text.size() && text[at] == '.') {
      ++at;
      int count = 0;
      while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        if (count < 7) {
          fraction = fraction * 10 + (text[at] - '0');
          ++count;
        }
        ++at;
      }
      if (count == 0) return std::nullopt;
      for (; count < 7; ++count) fraction *= 10;
    }

    std::int64_t offsetSeconds = 0;
    if (at < text.size() && text[at] == 'Z') {
      ++at;
    } else if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
      const int sign = text[at] == '-' ? -1 : 1;
      int oh, om;
      if (!digits(text, at + 1, 2, oh) || at + 3 >= text.size() || text[at + 3] != ':' ||
          !digits(text, at + 4, 2, om))
        return std::nullopt;
      offsetSeconds = sign * (oh * 3600 + om * 60);
      at += 6;
    } else {
      // No zone: the server always writes UTC.
    }
    if (at != text.size()) return std::nullopt;

    const std::int64_t days =
        daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const std::int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second - offsetSeconds;
    return ServerInstant::fromSinceOrigin(WallTicks(seconds * 10'000'000 + fraction));
  }

  std::string formatServerTime(ServerInstant instant) {
    const std::int64_t ticks = instant.sinceOrigin().count();
    std::int64_t seconds = ticks / 10'000'000;
    std::int64_t fraction = ticks % 10'000'000;
    if (fraction < 0) {
      fraction += 10'000'000;
      --seconds;
    }
    std::int64_t days = seconds / 86400;
    std::int64_t rest = seconds % 86400;
    if (rest < 0) {
      rest += 86400;
      --days;
    }
    std::int64_t year;
    unsigned month, day;
    civilFromDays(days, year, month, day);
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02uT%02lld:%02lld:%02lld.%07lldZ",
                  static_cast<long long>(year), month, day, static_cast<long long>(rest / 3600),
                  static_cast<long long>(rest / 60 % 60), static_cast<long long>(rest % 60),
                  static_cast<long long>(fraction));
    return buffer;
  }

  std::optional<WireMessage> parseMessage(std::string_view json) {
    const Json message = Json::parse(json, nullptr, false);
    if (message.is_discarded() || !message.is_object()) return std::nullopt;
    const std::string type = stringField(message, "MessageType");
    const Json* data = field(message, "Data");
    if (type == "SyncPlayCommand") {
      if (!data) return std::nullopt;
      return parseCommand(*data);
    }
    if (type == "SyncPlayGroupUpdate") {
      if (!data) return std::nullopt;
      return parseGroupUpdate(*data);
    }
    return wire::Other{type};
  }

  HttpRequest requestFor(const SyncPlayMessage& message) {
    return std::visit(
        [](const auto& m) -> HttpRequest {
          using T = std::decay_t<decltype(m)>;
          if constexpr (std::is_same_v<T, outgoing::Ready>) {
            return {HttpRequest::Method::Post, "/SyncPlay/Ready", reportBody(m)};
          } else if constexpr (std::is_same_v<T, outgoing::Buffering>) {
            return {HttpRequest::Method::Post, "/SyncPlay/Buffering", reportBody(m)};
          } else if constexpr (std::is_same_v<T, outgoing::RequestPause>) {
            return {HttpRequest::Method::Post, "/SyncPlay/Pause", {}};
          } else if constexpr (std::is_same_v<T, outgoing::RequestUnpause>) {
            return {HttpRequest::Method::Post, "/SyncPlay/Unpause", {}};
          } else if constexpr (std::is_same_v<T, outgoing::RequestSeek>) {
            return {HttpRequest::Method::Post, "/SyncPlay/Seek", positionBody("PositionTicks", m.position)};
          } else if constexpr (std::is_same_v<T, outgoing::RequestPlaybackRate>) {
            Json body = Json::object();
            body["PlaybackRate"] = m.rate.value();
            return {HttpRequest::Method::Post, "/SyncPlay/SetPlaybackRate", body.dump()};
          } else if constexpr (std::is_same_v<T, outgoing::RequestStop>) {
            return {HttpRequest::Method::Post, "/SyncPlay/Stop", {}};
          } else if constexpr (std::is_same_v<T, outgoing::RequestNextItem>) {
            return {HttpRequest::Method::Post, "/SyncPlay/NextItem", itemBody(m.playlistItem)};
          } else {
            static_assert(std::is_same_v<T, outgoing::RequestPreviousItem>);
            return {HttpRequest::Method::Post, "/SyncPlay/PreviousItem", itemBody(m.playlistItem)};
          }
        },
        message);
  }

  HttpRequest createGroupRequest(std::string_view name) {
    Json body = Json::object();
    body["GroupName"] = std::string(name);
    return {HttpRequest::Method::Post, "/SyncPlay/New", body.dump()};
  }

  HttpRequest joinGroupRequest(const GroupId& group) {
    Json body = Json::object();
    body["GroupId"] = group.value();
    return {HttpRequest::Method::Post, "/SyncPlay/Join", body.dump()};
  }

  HttpRequest leaveGroupRequest() { return {HttpRequest::Method::Post, "/SyncPlay/Leave", {}}; }

  HttpRequest pingRequest(WallTicks roundTrip) {
    Json body = Json::object();
    body["Ping"] = std::chrono::duration_cast<std::chrono::milliseconds>(roundTrip).count();
    return {HttpRequest::Method::Post, "/SyncPlay/Ping", body.dump()};
  }

  HttpRequest utcTimeRequest() { return {HttpRequest::Method::Get, "/GetUtcTime", {}}; }

  std::optional<UtcTimeReply> parseUtcTimeReply(std::string_view json) {
    const Json reply = Json::parse(json, nullptr, false);
    if (reply.is_discarded()) return std::nullopt;
    const auto received = timeField(reply, "RequestReceptionTime");
    const auto sent = timeField(reply, "ResponseTransmissionTime");
    if (!received || !sent) return std::nullopt;
    return UtcTimeReply{*received, *sent};
  }

  int parseUnsupportedParticipants(std::string_view json) {
    const Json reply = Json::parse(json, nullptr, false);
    if (reply.is_discarded()) return 0;
    for (const char* name : {"unsupportedParticipants", "UnsupportedParticipants"})
      if (const auto value = integerField(reply, name))
        return static_cast<int>(std::max<std::int64_t>(0, std::min<std::int64_t>(*value, 1'000'000)));
    return 0;
  }

} // namespace jfd::syncplay
