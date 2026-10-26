#include <jfd/music/MusicQueue.h>

#include <QJsonArray>

#include <algorithm>

namespace jfd::music {

  Track Track::fromItem(const api::Item& item) {
    Track track;
    track.id = item.id;
    track.name = item.name;
    track.album = item.album;
    track.albumId = item.albumId;
    track.artist = item.artists.isEmpty() ? item.albumArtist : item.artists.join(QStringLiteral(", "));
    track.albumArtist = item.albumArtist;
    if (!item.albumId.isEmpty() && !item.albumPrimaryImageTag.isEmpty()) {
      track.imageItemId = item.albumId;
      track.imageTag = item.albumPrimaryImageTag;
    } else if (!item.primaryImageTag.isEmpty()) {
      track.imageItemId = item.id;
      track.imageTag = item.primaryImageTag;
    }
    track.runTimeTicks = item.runTimeTicks.value_or(0);
    track.normalizationGain = item.normalizationGain;
    return track;
  }

  QJsonObject Track::toJson() const {
    QJsonObject o{{QStringLiteral("id"), id},
                  {QStringLiteral("name"), name},
                  {QStringLiteral("album"), album},
                  {QStringLiteral("albumId"), albumId},
                  {QStringLiteral("artist"), artist},
                  {QStringLiteral("albumArtist"), albumArtist},
                  {QStringLiteral("imageItemId"), imageItemId},
                  {QStringLiteral("imageTag"), imageTag},
                  {QStringLiteral("runTime"), static_cast<double>(runTimeTicks)},
                  {QStringLiteral("entry"), static_cast<double>(entryId)}};
    if (normalizationGain) o.insert(QStringLiteral("gain"), *normalizationGain);
    return o;
  }

  std::optional<Track> Track::fromJson(const QJsonObject& o) {
    Track track;
    track.id = o.value(QLatin1String("id")).toString();
    if (track.id.isEmpty()) return std::nullopt;
    track.name = o.value(QLatin1String("name")).toString();
    track.album = o.value(QLatin1String("album")).toString();
    track.albumId = o.value(QLatin1String("albumId")).toString();
    track.artist = o.value(QLatin1String("artist")).toString();
    track.albumArtist = o.value(QLatin1String("albumArtist")).toString();
    track.imageItemId = o.value(QLatin1String("imageItemId")).toString();
    track.imageTag = o.value(QLatin1String("imageTag")).toString();
    track.runTimeTicks = static_cast<std::int64_t>(o.value(QLatin1String("runTime")).toDouble());
    track.entryId = static_cast<std::int64_t>(o.value(QLatin1String("entry")).toDouble());
    if (o.contains(QLatin1String("gain"))) track.normalizationGain = o.value(QLatin1String("gain")).toDouble();
    return track;
  }

  const Track* MusicQueue::currentTrack() const {
    return m_current >= 0 && m_current < size() ? &m_tracks[m_current] : nullptr;
  }

  int MusicQueue::indexOfEntry(std::int64_t entryId) const {
    for (int i = 0; i < size(); ++i)
      if (m_tracks[i].entryId == entryId) return i;
    return -1;
  }

  void MusicQueue::assignEntries(QList<Track>& tracks) {
    for (Track& track : tracks)
      track.entryId = m_nextEntry++;
  }

  // Mixes the tracks from `first` on.
  void MusicQueue::shuffleAfter(int first) {
    if (first < size()) std::shuffle(m_tracks.begin() + first, m_tracks.end(), m_random);
  }

  void MusicQueue::setShuffle(bool on) {
    if (on == m_shuffle) return;
    m_shuffle = on;
    if (on) {
      m_original.clear();
      for (const Track& track : m_tracks)
        m_original.append(track.entryId);
      shuffleAfter(m_current + 1);
      return;
    }
    // Back to the order of adding; tracks added since sit at the end.
    const std::int64_t currentEntry = currentTrack() ? currentTrack()->entryId : 0;
    QList<Track> restored;
    restored.reserve(m_tracks.size());
    QList<bool> used(m_tracks.size(), false);
    for (const std::int64_t entry : m_original) {
      const int index = indexOfEntry(entry);
      if (index >= 0 && !used[index]) {
        restored.append(m_tracks[index]);
        used[index] = true;
      }
    }
    for (int i = 0; i < size(); ++i)
      if (!used[i]) restored.append(m_tracks[i]);
    m_tracks = std::move(restored);
    m_original.clear();
    m_current = currentEntry ? indexOfEntry(currentEntry) : -1;
  }

  void MusicQueue::replace(QList<Track> tracks, int start) {
    assignEntries(tracks);
    m_tracks = std::move(tracks);
    m_original.clear();
    m_current = m_tracks.isEmpty() ? -1 : std::clamp(start, 0, size() - 1);
    if (!m_shuffle || m_tracks.isEmpty()) return;
    for (const Track& track : m_tracks)
      m_original.append(track.entryId);
    // The chosen track first, the rest mixed.
    std::swap(m_tracks[0], m_tracks[m_current]);
    m_current = 0;
    shuffleAfter(1);
  }

  void MusicQueue::append(QList<Track> tracks) {
    assignEntries(tracks);
    for (Track& track : tracks) {
      if (m_shuffle) m_original.append(track.entryId);
      m_tracks.append(std::move(track));
    }
  }

  void MusicQueue::playNext(QList<Track> tracks) {
    assignEntries(tracks);
    const int at = m_current + 1;
    if (m_shuffle) {
      // In the original order they follow the current track too.
      int originalAt = 0;
      if (const Track* now = currentTrack()) originalAt = static_cast<int>(m_original.indexOf(now->entryId)) + 1;
      for (const Track& track : tracks)
        m_original.insert(originalAt++, track.entryId);
    }
    for (int i = 0; i < tracks.size(); ++i)
      m_tracks.insert(at + i, tracks[i]);
  }

  MusicQueue::Removed MusicQueue::remove(int index) {
    if (index < 0 || index >= size()) return Removed::Other;
    const std::int64_t entry = m_tracks[index].entryId;
    m_tracks.removeAt(index);
    m_original.removeOne(entry);
    if (index < m_current) {
      --m_current;
      return Removed::Other;
    }
    if (index > m_current) return Removed::Other;
    if (m_current >= size()) m_current = size() - 1;
    return Removed::Current;
  }

  void MusicQueue::move(int from, int to) {
    if (from < 0 || from >= size() || to < 0 || to >= size() || from == to) return;
    const std::int64_t currentEntry = currentTrack() ? currentTrack()->entryId : 0;
    m_tracks.move(from, to);
    if (currentEntry) m_current = indexOfEntry(currentEntry);
  }

  void MusicQueue::clear() {
    m_tracks.clear();
    m_original.clear();
    m_current = -1;
  }

  void MusicQueue::setCurrent(int index) { m_current = index >= 0 && index < size() ? index : -1; }

  std::optional<int> MusicQueue::nextIndex(bool automatic) const {
    if (isEmpty()) return std::nullopt;
    if (automatic && m_repeat == Repeat::Track && m_current >= 0) return m_current;
    if (m_current + 1 < size()) return m_current + 1;
    if (m_repeat == Repeat::Queue || m_repeat == Repeat::Track) return 0;
    return std::nullopt;
  }

  std::optional<int> MusicQueue::previousIndex() const {
    if (isEmpty()) return std::nullopt;
    if (m_current > 0) return m_current - 1;
    if (m_repeat == Repeat::Queue) return size() - 1;
    return std::nullopt;
  }

  QJsonObject MusicQueue::toJson() const {
    QJsonArray tracks;
    for (const Track& track : m_tracks)
      tracks.append(track.toJson());
    QJsonArray original;
    for (const std::int64_t entry : m_original)
      original.append(static_cast<double>(entry));
    const char* repeat = m_repeat == Repeat::Queue ? "queue" : m_repeat == Repeat::Track ? "track" : "none";
    return {{QStringLiteral("tracks"), tracks},
            {QStringLiteral("original"), original},
            {QStringLiteral("current"), m_current},
            {QStringLiteral("repeat"), QLatin1String(repeat)},
            {QStringLiteral("shuffle"), m_shuffle},
            {QStringLiteral("nextEntry"), static_cast<double>(m_nextEntry)}};
  }

  std::optional<MusicQueue> MusicQueue::fromJson(const QJsonObject& o) {
    if (!o.value(QLatin1String("tracks")).isArray()) return std::nullopt;
    MusicQueue queue;
    std::int64_t highest = 0;
    for (const QJsonValue& value : o.value(QLatin1String("tracks")).toArray()) {
      auto track = Track::fromJson(value.toObject());
      if (!track) return std::nullopt;
      highest = std::max(highest, track->entryId);
      queue.m_tracks.append(std::move(*track));
    }
    for (const QJsonValue& value : o.value(QLatin1String("original")).toArray())
      queue.m_original.append(static_cast<std::int64_t>(value.toDouble()));
    queue.m_shuffle = o.value(QLatin1String("shuffle")).toBool();
    if (!queue.m_shuffle) queue.m_original.clear();
    const QString repeat = o.value(QLatin1String("repeat")).toString();
    queue.m_repeat = repeat == QLatin1String("queue")   ? Repeat::Queue
                     : repeat == QLatin1String("track") ? Repeat::Track
                                                        : Repeat::None;
    queue.m_nextEntry = std::max(static_cast<std::int64_t>(o.value(QLatin1String("nextEntry")).toDouble()), highest + 1);
    queue.setCurrent(o.value(QLatin1String("current")).toInt(-1));
    return queue;
  }

} // namespace jfd::music
