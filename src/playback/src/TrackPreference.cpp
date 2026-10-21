#include <jfd/playback/TrackPreference.h>

#include <algorithm>

namespace jfd::playback {

  namespace {
    QString typeOf(core::TrackKind kind) {
      return kind == core::TrackKind::Audio ? QStringLiteral("Audio") : QStringLiteral("Subtitle");
    }
  } // namespace

  void TrackPreference::remember(core::TrackKind kind, const api::MediaSource& source,
                                 std::optional<int> streamIndex) {
    const QString type = typeOf(kind);
    std::optional<Choice> choice;
    if (streamIndex)
      for (const api::MediaStream& stream : source.mediaStreams)
        if (stream.index == *streamIndex && stream.type == type) {
          choice = Choice{stream.language, stream.title, stream.isExternal, stream.isForced};
          break;
        }
    if (kind == core::TrackKind::Audio) {
      if (choice) m_audio = choice;
    } else if (choice || !streamIndex) {
      m_subtitle = choice;
    }
  }

  std::optional<int> TrackPreference::match(const api::MediaSource& source, const QString& type,
                                            const Choice& choice) {
    // The language decides; without one (untagged tracks) only the title can.
    std::optional<int> best;
    int bestScore = -1;
    for (const api::MediaStream& stream : source.mediaStreams) {
      if (stream.type != type) continue;
      if (choice.language.isEmpty() ? (choice.title.isEmpty() || stream.title != choice.title)
                                    : stream.language.compare(choice.language, Qt::CaseInsensitive) != 0)
        continue;
      const int score = (stream.isExternal == choice.external ? 8 : 0) +
                        (stream.isForced == choice.forced ? 4 : 0) +
                        (!choice.title.isEmpty() && stream.title == choice.title ? 2 : 0) +
                        (stream.isDefault ? 1 : 0);
      if (score > bestScore) {
        bestScore = score;
        best = stream.index;
      }
    }
    return best;
  }

  void TrackPreference::choose(const api::MediaSource& source, std::optional<int>& audioStreamIndex,
                               std::optional<int>& subtitleStreamIndex) const {
    // The source's default needs no choice: leaving it empty spares a
    // transcode a second PlaybackInfo request.
    if (!audioStreamIndex && m_audio)
      if (const auto index = match(source, typeOf(core::TrackKind::Audio), *m_audio);
          index && index != source.defaultAudioStreamIndex)
        audioStreamIndex = index;
    if (!subtitleStreamIndex && m_subtitle) {
      // -1 or empty: the server shows none.
      const int serverDefault = std::max(source.defaultSubtitleStreamIndex.value_or(-1), -1);
      if (!*m_subtitle) {
        if (serverDefault >= 0) subtitleStreamIndex = -1;
      } else if (const auto index = match(source, typeOf(core::TrackKind::Subtitle), **m_subtitle);
                 index && *index != serverDefault) {
        subtitleStreamIndex = index;
      }
    }
  }

} // namespace jfd::playback
