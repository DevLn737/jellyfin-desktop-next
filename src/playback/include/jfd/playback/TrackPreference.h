#pragma once

#include <jfd/api/Models.h>
#include <jfd/core/MediaBackend.h>

#include <QString>

#include <optional>

// The audio and subtitles the viewer picked last, carried to the next file:
// the next episode, or the item a SyncPlay group switches to. Each viewer
// keeps their own, so in a group everyone can watch with their own language
// and their own external files. Nothing is chosen before the viewer picks:
// until then the server's defaults (the user's language settings) apply.
namespace jfd::playback {

  class TrackPreference {
  public:
    // The viewer picked `streamIndex` of `source` (empty: subtitles off).
    void remember(core::TrackKind kind, const api::MediaSource& source,
                  std::optional<int> streamIndex);

    // Fills the indices that are empty with the remembered choice, where
    // `source` has a matching track and it is not the source's default
    // already. -1 turns subtitles off.
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

    std::optional<Choice> m_audio;
    // Set once the viewer picked subtitles; an empty choice is "off".
    std::optional<std::optional<Choice>> m_subtitle;
  };

} // namespace jfd::playback
