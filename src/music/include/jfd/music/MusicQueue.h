#pragma once

#include <jfd/api/Models.h>

#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>
#include <random>

// The music play queue: tracks in the order they play, the track playing
// now, repeat and shuffle. No network and no player; MusicPlayer drives the
// session from it. With shuffle on, the queue itself is shuffled (the
// current track stays where it is, what follows is mixed), so the list the
// user sees is the order that plays; turning shuffle off restores the order
// the tracks were added in.
namespace jfd::music {

  // What the queue, the mini-player and the Now Playing page need of a
  // track; small enough to keep tens of thousands of them.
  struct Track {
    QString id;
    QString name;
    QString album;
    QString albumId;
    // The artists as shown: "A, B".
    QString artist;
    QString albumArtist;
    // The item whose Primary image is the cover, and its tag.
    QString imageItemId;
    QString imageTag;
    std::int64_t runTimeTicks = 0;
    // The server's ReplayGain-style gain in dB, when the file has one.
    std::optional<double> normalizationGain;
    // Unique in a queue; assigned by it. The same track queued twice has
    // two entries.
    std::int64_t entryId = 0;

    static Track fromItem(const api::Item& item);
    QJsonObject toJson() const;
    static std::optional<Track> fromJson(const QJsonObject& object);
  };

  enum class Repeat { None, Queue, Track };

  class MusicQueue {
  public:
    const QList<Track>& tracks() const { return m_tracks; }
    int size() const { return static_cast<int>(m_tracks.size()); }
    bool isEmpty() const { return m_tracks.isEmpty(); }
    // -1 when no track is chosen.
    int current() const { return m_current; }
    const Track* currentTrack() const;
    int indexOfEntry(std::int64_t entryId) const;

    Repeat repeat() const { return m_repeat; }
    void setRepeat(Repeat repeat) { m_repeat = repeat; }
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on);
    // Makes shuffling repeatable in tests.
    void seed(std::uint32_t value) { m_random.seed(value); }

    // Replaces the queue and chooses `start`. With shuffle on that track
    // plays first and the others follow mixed.
    void replace(QList<Track> tracks, int start);
    // At the end.
    void append(QList<Track> tracks);
    // Right after the current track (at the start when none is chosen).
    void playNext(QList<Track> tracks);
    enum class Removed { Other, Current };
    // Removing the current track leaves current() on the track that
    // followed it (or the last one), so playback can go on from there.
    Removed remove(int index);
    void move(int from, int to);
    void clear();
    void setCurrent(int index);

    // The track after the current one. `automatic`: the current one ended
    // by itself, so Repeat::Track repeats it; a skip goes on.
    std::optional<int> nextIndex(bool automatic) const;
    // The track before the current one; wraps with Repeat::Queue.
    std::optional<int> previousIndex() const;

    QJsonObject toJson() const;
    // Empty on a malformed document.
    static std::optional<MusicQueue> fromJson(const QJsonObject& object);

  private:
    void assignEntries(QList<Track>& tracks);
    void shuffleAfter(int first);

    QList<Track> m_tracks;
    // The order the tracks were added in, as entry ids; only while shuffled.
    QList<std::int64_t> m_original;
    int m_current = -1;
    Repeat m_repeat = Repeat::None;
    bool m_shuffle = false;
    std::int64_t m_nextEntry = 1;
    std::mt19937 m_random{std::random_device{}()};
  };

} // namespace jfd::music
