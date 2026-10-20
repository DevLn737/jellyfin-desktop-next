#pragma once

#include <QJsonObject>

// The device profile sent with PlaybackInfo and session capabilities. Ported
// from getDeviceProfile() in the 2.x client's native/nativeshell.js: mpv plays
// nearly every container and codec, so the profile allows direct play of
// everything and only narrows it through these user settings.
namespace jfd::api {

  struct DeviceProfileOptions {
    // Video is always transcoded (DirectPlayProfiles omit Video).
    bool alwaysForceTranscode = false;
    // Transcode Dolby Vision, HDR, 10-bit H.264, HEVC, AV1 or anything above
    // 1080p instead of playing it directly.
    bool forceTranscodeDolbyVision = false;
    bool forceTranscodeHdr = false;
    bool forceTranscodeHi10p = false;
    bool forceTranscodeHevc = false;
    bool forceTranscodeAv1 = false;
    bool forceTranscode4k = false;
    // Let the server transcode to HEVC, and prefer it over H.264.
    bool allowTranscodeToHevc = false;
    bool preferTranscodeToHevc = false;
    // Audio channels allowed in a transcode: 2 for stereo output, else 6.
    int maxTranscodeAudioChannels = 6;
  };

  QJsonObject buildDeviceProfile(const DeviceProfileOptions& options);

} // namespace jfd::api
