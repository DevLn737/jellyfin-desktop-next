#pragma once

#include <jfd/api/Models.h>
#include <jfd/core/MediaBackend.h>

#include <QString>

#include <optional>

// Which audio and subtitles a file opens with. First the viewer's last pick,
// carried to the next file: the next episode, or the item a SyncPlay group
// switches to. Each viewer keeps their own, so in a group everyone can watch
// with their own language and their own external files. Before any pick,
// the playback settings: preferred languages and the subtitle mode. Where
// neither decides, the server's defaults (the user's settings on the
// server) apply.
namespace jfd::playback {

  enum class SubtitleMode {
    // The server's choice.
    Default,
    // Subtitles in the preferred language whenever there are some.
    Always,
    // Only forced subtitles (signs, foreign dialogue).
    OnlyForced,
    Off,
  };

  struct TrackDefaults {
    // ISO 639-2 codes as Jellyfin reports them ("rus", "ger"); empty for no
    // preference. B and T codes ("ger", "deu") match each other.
    QString audioLanguage;
    QString subtitleLanguage;
    SubtitleMode subtitleMode = SubtitleMode::Default;
  };

  // The same language: equal ISO 639 codes in any of their forms (ISO
  // 639-1, 639-2/B, 639-2/T), ignoring case.
  bool sameLanguage(const QString& a, const QString& b);

  class TrackPreference {
  public:
    void setDefaults(TrackDefaults defaults) { m_defaults = std::move(defaults); }
    const TrackDefaults& defaults() const { return m_defaults; }

    // The viewer picked `streamIndex` of `source` (empty: subtitles off).
    void remember(core::TrackKind kind, const api::MediaSource& source,
                  std::optional<int> streamIndex);

    // Fills the indices that are empty with the remembered pick or the
    // defaults, where `source` has a matching track and it is not the
    // source's default already. -1 turns subtitles off.
    void choose(const api::MediaSource& source, std::optional<int>& audioStreamIndex,
                std::optional<int>& subtitleStreamIndex) const;

  private:
    struct Choice {
      QString language;
      QString title;
      bool external = false;
      bool forced = false;
    };

    static std::optional<int> match(const api::MediaSource& source, const QString& type,
                                    const Choice& choice);
    std::optional<int> defaultSubtitle(const api::MediaSource& source, int serverDefault) const;

    TrackDefaults m_defaults;
    std::optional<Choice> m_audio;
    // Set once the viewer picked subtitles; an empty choice is "off".
    std::optional<std::optional<Choice>> m_subtitle;
  };

} // namespace jfd::playback
