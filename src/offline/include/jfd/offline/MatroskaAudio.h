#pragma once

#include <QString>

// Keeps only the audio of a Matroska file.
//
// Jellyfin 12.2 has no request that returns an external audio file (a
// "Film.ru.mka" next to the film) as it is: its audio-only streams leave
// the track choice to ffmpeg, which takes the video's own audio. A video
// stream maps the external track correctly, so the downloader asks for one
// with the audio copied and the picture shrunk to a few kilobytes, and this
// drops the picture locally.
//
// The output has the EBML header, the segment's Info (with the duration
// counted from the blocks), the audio TrackEntries (Language set when given),
// the clusters with only audio blocks, Cues for each cluster and a SeekHead.
// Tags, chapters, attachments and the input's own Cues and SeekHead are not
// copied. Elements of unknown size (a live writer's clusters) are read up to
// the next top-level element.
namespace jfd::offline {

  // Writes `output` (replacing it) from `input`. `language` (ISO 639-2, may
  // be empty) is set on every audio track. False, with `error` set, when the
  // input is not Matroska, has no audio track, or a file cannot be read or
  // written; a partial output is removed.
  bool extractMatroskaAudio(const QString& input, const QString& output, const QString& language,
                            QString* error = nullptr);

} // namespace jfd::offline
