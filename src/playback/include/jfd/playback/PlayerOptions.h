#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The audio and subtitle settings as mpv properties, and the 2.x client's
// values for the playback settings. Every function returns the whole set of
// properties, defaults included, so a setting turned back to "default"
// undoes what it set before.
namespace jfd::playback {

  using MpvProperty = QPair<QByteArray, QByteArray>;

  struct AudioOptions {
    // An mpv audio-device name; "auto" for the system's default.
    QString device = QStringLiteral("auto");
    // An mpv audio-channels value: "auto-safe", "2.0", "5.1,2.0", "7.1,5.1,2.0".
    QString channels = QStringLiteral("auto-safe");
    // Codecs sent to the receiver as is: ac3, eac3, dts, dts-hd, truehd.
    QStringList passthrough;
    bool normalizeDownmix = false;
    bool exclusive = false;
  };

  // The passthrough codecs in the order mpv's audio-spdif takes them.
  const QStringList& passthroughCodecs();

  // A device missing from availableDevices (unplugged) plays on "auto".
  QList<MpvProperty> audioProperties(const AudioOptions& options, const QStringList& availableDevices);

  struct SubtitleStyle {
    // Empty strings and -1 mean mpv's default.
    QString font;
    // "#RRGGBB".
    QString color;
    QString borderColor;
    int borderSize = -1;
    QString backgroundColor;
    // The background's alpha as two hex digits ("80"); empty is opaque.
    QString backgroundAlpha;
    // "<left|center|right>,<top|bottom>", as in 2.x.
    QString placement;
    // mpv sub-font-size; -1 for mpv's own.
    int size = -1;
    // mpv sub-ass-override; empty for "scale".
    QString assOverride;
    bool assScaleBorderAndShadow = true;
  };

  QList<MpvProperty> subtitleProperties(const SubtitleStyle& style);

  // The 2.x client's settings ("section.key" as in its
  // settings_description.json, with its stored values) as this client's
  // playback, audio and subtitle settings ("section/key"). Keys 2.x did not
  // set, or values this client has no counterpart for, are left out.
  QVariantMap importLegacyPlayback(const QVariantMap& legacy);

} // namespace jfd::playback
