#include <jfd/playback/TrackTable.h>

namespace jfd::playback {

  namespace {
    std::optional<TrackKind> kindOf(const QString& type) {
      if (type == QLatin1String("Audio")) return TrackKind::Audio;
      if (type == QLatin1String("Subtitle")) return TrackKind::Subtitle;
      return std::nullopt;
    }

    const api::ExternalTrack* externalFor(const QList<api::ExternalTrack>& list, int index) {
      for (const api::ExternalTrack& track : list)
        if (track.streamIndex == index) return &track;
      return nullptr;
    }
  } // namespace

  TrackTable::TrackTable(const api::MediaSource& source, const api::PlaybackStream& stream)
      : m_transcoded(stream.method == api::PlayMethod::Transcode) {
    for (const api::MediaStream& s : source.mediaStreams) {
      const auto kind = kindOf(s.type);
      if (!kind) continue;
      Entry entry;
      entry.info = {s.index,   *kind,       s.displayTitle.isEmpty() ? s.title : s.displayTitle,
                    s.language, s.codec,    s.isDefault,
                    s.isForced, s.isExternal || s.deliveryMethod == QLatin1String("External")};
      entry.deliveryMethod = s.deliveryMethod;
      const auto& externals =
          *kind == TrackKind::Audio ? stream.externalAudio : stream.externalSubtitles;
      if (const api::ExternalTrack* external = externalFor(externals, s.index)) {
        entry.url = external->url;
      } else if (entry.info.isExternal) {
        // External, but the server gave no URL mpv can open.
        continue;
      } else if (m_transcoded) {
        // Only the tracks the stream was opened with are in a transcode;
        // selectInitial() marks them.
        entry.inStream = false;
      } else {
        entry.trackId = api::embeddedTrackId(source, s.index);
        if (!entry.trackId) continue;
      }
      m_entries.push_back(entry);
    }
  }

  TrackTable::Entry* TrackTable::find(TrackKind kind, int streamIndex) {
    for (Entry& entry : m_entries)
      if (entry.info.kind == kind && entry.info.streamIndex == streamIndex) return &entry;
    return nullptr;
  }

  const TrackTable::Entry* TrackTable::find(TrackKind kind, int streamIndex) const {
    return const_cast<TrackTable*>(this)->find(kind, streamIndex);
  }

  QList<TrackInfo> TrackTable::tracks(TrackKind kind) const {
    QList<TrackInfo> result;
    for (const Entry& entry : m_entries)
      if (entry.info.kind == kind && !entry.failed) result.append(entry.info);
    return result;
  }

  std::optional<int> TrackTable::selected(TrackKind kind) const {
    return kind == TrackKind::Audio ? m_audio : m_subtitle;
  }

  bool TrackTable::failed(int streamIndex) const {
    for (const Entry& entry : m_entries)
      if (entry.info.streamIndex == streamIndex) return entry.failed;
    return false;
  }

  std::vector<Command> TrackTable::selectInitial(std::optional<int> audio,
                                                 std::optional<int> subtitle) {
    if (audio && !find(TrackKind::Audio, *audio)) audio.reset();
    if (subtitle && !find(TrackKind::Subtitle, *subtitle)) subtitle.reset();
    if (!audio && !m_transcoded)
      for (const Entry& entry : m_entries)
        if (entry.info.kind == TrackKind::Audio && entry.trackId) {
          audio = entry.info.streamIndex;
          break;
        }
    if (m_transcoded) {
      // A transcode carries one audio track, and the subtitle it was opened
      // with either as a track of its own or burned into the picture.
      if (audio)
        if (Entry* entry = find(TrackKind::Audio, *audio); entry && !entry->url) {
          entry->inStream = true;
          entry->trackId = 1;
        }
      if (subtitle)
        if (Entry* entry = find(TrackKind::Subtitle, *subtitle); entry && !entry->url) {
          entry->inStream = true;
          if (entry->deliveryMethod == QLatin1String("Encode"))
            entry->burnedIn = true;
          else
            entry->trackId = 1;
        }
    }
    m_audio = audio;
    m_subtitle = subtitle;
    std::vector<Command> commands = apply(TrackKind::Audio);
    for (Command& command : apply(TrackKind::Subtitle)) commands.push_back(std::move(command));
    return commands;
  }

  std::vector<Command> TrackTable::select(TrackKind kind, std::optional<int> streamIndex) {
    if (streamIndex) {
      const Entry* entry = find(kind, *streamIndex);
      if (!entry || entry->failed) return {};
    }
    (kind == TrackKind::Audio ? m_audio : m_subtitle) = streamIndex;
    return apply(kind);
  }

  std::vector<Command> TrackTable::apply(TrackKind kind) {
    const std::optional<int> wanted = selected(kind);
    if (!wanted) {
      // Audio is never switched off; without a choice mpv keeps its own.
      if (kind == TrackKind::Audio) return {};
      return {command::Select{kind, std::nullopt}};
    }
    Entry* entry = find(kind, *wanted);
    if (!entry) return {};
    if (!entry->inStream) return {command::Reopen{m_audio, m_subtitle.value_or(-1)}};
    if (entry->burnedIn) return {command::Select{kind, std::nullopt}};
    if (entry->trackId) return {command::Select{kind, entry->trackId}};
    if (!entry->url || entry->pendingKey) return {};
    entry->pendingKey = m_nextKey++;
    m_pending[entry->pendingKey] = {kind, entry->info.streamIndex};
    return {command::Add{entry->pendingKey,
                         {kind, entry->url->toString(QUrl::FullyEncoded).toStdString(),
                          entry->info.title.toStdString(), entry->info.language.toStdString()}}};
  }

  std::vector<Command> TrackTable::trackAdded(std::uint64_t key, int trackId) {
    const auto pending = m_pending.find(key);
    if (pending == m_pending.end()) return {};
    const auto [kind, index] = pending->second;
    m_pending.erase(pending);
    Entry* entry = find(kind, index);
    entry->pendingKey = 0;
    entry->trackId = trackId;
    // mpv selected the track it added; if the choice has moved on since,
    // this puts it back.
    return apply(kind);
  }

  std::vector<Command> TrackTable::trackFailed(std::uint64_t key) {
    const auto pending = m_pending.find(key);
    if (pending == m_pending.end()) return {};
    const auto [kind, index] = pending->second;
    m_pending.erase(pending);
    Entry* entry = find(kind, index);
    entry->pendingKey = 0;
    entry->failed = true;
    std::optional<int>& wanted = kind == TrackKind::Audio ? m_audio : m_subtitle;
    if (wanted != index) return {};
    // Falls back to an audio track the stream carries; subtitles go off.
    wanted.reset();
    if (kind == TrackKind::Audio)
      for (const Entry& candidate : m_entries)
        if (candidate.info.kind == TrackKind::Audio && candidate.trackId && candidate.inStream &&
            !candidate.failed) {
          wanted = candidate.info.streamIndex;
          break;
        }
    return apply(kind);
  }

} // namespace jfd::playback
