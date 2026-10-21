#include <jfd/playback/TrackPreference.h>

#include <algorithm>
#include <array>

namespace jfd::playback {

  namespace {
    QString typeOf(core::TrackKind kind) {
      return kind == core::TrackKind::Audio ? QStringLiteral("Audio") : QStringLiteral("Subtitle");
    }

    // ISO 639-1, 639-2/B and 639-2/T codes of one language, where they
    // differ or files commonly carry the short form.
    struct Codes {
      const char* one;
      const char* bibliographic;
      const char* terminology;
    };
    constexpr std::array<Codes, 32> Languages{{
        {"ru", "rus", "rus"}, {"en", "eng", "eng"}, {"ja", "jpn", "jpn"}, {"uk", "ukr", "ukr"},
        {"de", "ger", "deu"}, {"fr", "fre", "fra"}, {"es", "spa", "spa"}, {"it", "ita", "ita"},
        {"ko", "kor", "kor"}, {"zh", "chi", "zho"}, {"pt", "por", "por"}, {"pl", "pol", "pol"},
        {"cs", "cze", "ces"}, {"nl", "dut", "nld"}, {"el", "gre", "ell"}, {"fa", "per", "fas"},
        {"ro", "rum", "ron"}, {"sk", "slo", "slk"}, {"cy", "wel", "cym"}, {"hy", "arm", "hye"},
        {"eu", "baq", "eus"}, {"ka", "geo", "kat"}, {"is", "ice", "isl"}, {"mk", "mac", "mkd"},
        {"ms", "may", "msa"}, {"my", "bur", "mya"}, {"bo", "tib", "bod"}, {"sq", "alb", "sqi"},
        {"be", "bel", "bel"}, {"kk", "kaz", "kaz"}, {"tr", "tur", "tur"}, {"ar", "ara", "ara"},
    }};

    // One form per language: the 639-2/B code where known.
    QString canonical(const QString& code) {
      const QString lower = code.trimmed().toLower();
      for (const Codes& codes : Languages)
        if (lower == QLatin1String(codes.one) || lower == QLatin1String(codes.bibliographic) ||
            lower == QLatin1String(codes.terminology))
          return QString::fromLatin1(codes.bibliographic);
      return lower;
    }

    // The first stream of `type` in `language` that `accept` takes,
    // preferring the source's own default flag.
    template <typename Accept>
    std::optional<int> inLanguage(const api::MediaSource& source, const QString& type,
                                  const QString& language, Accept accept) {
      std::optional<int> found;
      for (const api::MediaStream& stream : source.mediaStreams) {
        if (stream.type != type || !sameLanguage(stream.language, language) || !accept(stream)) continue;
        if (stream.isDefault) return stream.index;
        if (!found) found = stream.index;
      }
      return found;
    }
  } // namespace

  bool sameLanguage(const QString& a, const QString& b) {
    return !a.trimmed().isEmpty() && canonical(a) == canonical(b);
  }

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
                                    : !sameLanguage(stream.language, choice.language))
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

  std::optional<int> TrackPreference::defaultSubtitle(const api::MediaSource& source,
                                                      int serverDefault) const {
    const QString type = typeOf(core::TrackKind::Subtitle);
    const QString& language = m_defaults.subtitleLanguage;
    switch (m_defaults.subtitleMode) {
    case SubtitleMode::Default:
      return std::nullopt;
    case SubtitleMode::Off:
      return -1;
    case SubtitleMode::Always:
      // Full subtitles before signs-only ones in that language.
      if (language.isEmpty()) return std::nullopt;
      if (const auto full = inLanguage(source, type, language, [](const api::MediaStream& s) { return !s.isForced; }))
        return full;
      return inLanguage(source, type, language, [](const api::MediaStream&) { return true; });
    case SubtitleMode::OnlyForced: {
      const auto forced = [](const api::MediaStream& s) { return s.isForced; };
      if (!language.isEmpty())
        if (const auto index = inLanguage(source, type, language, forced)) return index;
      // Forced subtitles are usually in the audio's language: any will do.
      for (const api::MediaStream& stream : source.mediaStreams)
        if (stream.type == type && stream.isForced) return stream.index;
      return serverDefault >= 0 ? std::optional<int>(-1) : std::nullopt;
    }
    }
    return std::nullopt;
  }

  void TrackPreference::choose(const api::MediaSource& source, std::optional<int>& audioStreamIndex,
                               std::optional<int>& subtitleStreamIndex) const {
    // The source's default needs no choice: leaving it empty spares a
    // transcode a second PlaybackInfo request.
    if (!audioStreamIndex) {
      std::optional<int> index;
      if (m_audio) index = match(source, typeOf(core::TrackKind::Audio), *m_audio);
      if (!index && !m_defaults.audioLanguage.isEmpty())
        index = inLanguage(source, typeOf(core::TrackKind::Audio), m_defaults.audioLanguage,
                           [](const api::MediaStream&) { return true; });
      if (index && index != source.defaultAudioStreamIndex) audioStreamIndex = index;
    }
    if (!subtitleStreamIndex) {
      // -1 or empty: the server shows none.
      const int serverDefault = std::max(source.defaultSubtitleStreamIndex.value_or(-1), -1);
      std::optional<int> index;
      if (!m_subtitle)
        index = defaultSubtitle(source, serverDefault);
      else if (!*m_subtitle)
        index = -1;
      else
        index = match(source, typeOf(core::TrackKind::Subtitle), **m_subtitle);
      if (index && *index != serverDefault) subtitleStreamIndex = index;
    }
  }

} // namespace jfd::playback
