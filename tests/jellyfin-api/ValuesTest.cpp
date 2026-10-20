#include "../support/DoctestQt.h"

#include <jfd/api/ClientIdentity.h>
#include <jfd/api/DeviceProfile.h>
#include <jfd/api/ServerTime.h>
#include <jfd/api/ServerVersion.h>

#include <QJsonArray>
#include <QUrl>

using namespace jfd;
using namespace jfd::api;

TEST_CASE("Authorization header carries the identity and percent-encodes values") {
  ClientIdentity identity{QStringLiteral("Jellyfin Desktop Next"), QStringLiteral("0.0.1"),
                          QStringLiteral("Alice's PC + \"Test\""), QStringLiteral("abc123")};
  const QByteArray header = authorizationHeader(identity, QStringLiteral("tok"));
  CHECK(header == "MediaBrowser Client=\"Jellyfin%20Desktop%20Next\", "
                  "Device=\"Alice%27s%20PC%20%2B%20%22Test%22\", "
                  "DeviceId=\"abc123\", Version=\"0.0.1\", Token=\"tok\"");
  // The server URL-decodes every value; decoding must give the original back.
  CHECK(QUrl::fromPercentEncoding("Alice%27s%20PC%20%2B%20%22Test%22") ==
        QStringLiteral("Alice's PC + \"Test\""));
  CHECK_FALSE(authorizationHeader(identity, {}).contains("Token"));
}

TEST_CASE("Device ids are random and non-empty") {
  const QString a = generateDeviceId();
  CHECK(a.size() == 32);
  CHECK(a != generateDeviceId());
}

TEST_CASE("Server instants keep all seven fractional digits") {
  const auto instant = parseServerInstant(u"2026-10-20T07:05:02.1234567Z");
  REQUIRE(instant);
  // 2026-10-20T07:05:02Z is 1792479902 s after the epoch.
  CHECK(instant->sinceOrigin().count() == 1'792'479'902LL * 10'000'000 + 1'234'567);
  CHECK(formatServerInstant(*instant) == QStringLiteral("2026-10-20T07:05:02.1234567Z"));
}

TEST_CASE("Server instants accept offsets, short fractions and no fraction") {
  const auto utc = parseServerInstant(u"2026-10-20T07:05:02.5Z");
  const auto offset = parseServerInstant(u"2026-10-20T10:05:02.5+03:00");
  REQUIRE(utc);
  REQUIRE(offset);
  CHECK(*utc == *offset);
  CHECK(utc->sinceOrigin().count() % 10'000'000 == 5'000'000);
  CHECK(parseServerInstant(u"1970-01-01T00:00:00Z")->sinceOrigin().count() == 0);
  CHECK(parseServerInstant(u"2026-10-20T07:05:02.123456789Z")->sinceOrigin().count() % 10'000'000 ==
        1'234'567);
  CHECK(formatServerInstant(*parseServerInstant(u"2000-02-29T23:59:59Z")) ==
        QStringLiteral("2000-02-29T23:59:59.0000000Z"));
}

TEST_CASE("Server instants without a zone or with garbage are rejected") {
  CHECK_FALSE(parseServerInstant(u"2026-10-20T07:05:02"));
  CHECK_FALSE(parseServerInstant(u"2026-10-20"));
  CHECK_FALSE(parseServerInstant(u"2026-13-20T07:05:02Z"));
  CHECK_FALSE(parseServerInstant(u"2026-10-20T07:05:02.Z"));
  CHECK_FALSE(parseServerInstant(u"2026-10-20T07:05:02Zjunk"));
  CHECK_FALSE(parseServerInstant(u""));
}

TEST_CASE("Server versions parse and compare") {
  CHECK(ServerVersion::parse(u"12.2.0") == ServerVersion{12, 2, 0});
  CHECK(ServerVersion::parse(u"10.10.7.0") == ServerVersion{10, 10, 7});
  CHECK(ServerVersion::parse(u"12.2.0-syncplay.1") == ServerVersion{12, 2, 0});
  CHECK(ServerVersion::parse(u"12.2") == ServerVersion{12, 2, 0});
  CHECK_FALSE(ServerVersion::parse(u"12"));
  CHECK_FALSE(ServerVersion::parse(u"abc"));
  CHECK(ServerVersion{10, 9, 11} < MinimumServerVersion);
  CHECK_FALSE(ServerVersion{12, 2, 0} < MinimumServerVersion);
}

TEST_CASE("Device profile allows direct play of everything by default") {
  const QJsonObject profile = buildDeviceProfile({});
  const QJsonArray directPlay = profile.value(QStringLiteral("DirectPlayProfiles")).toArray();
  CHECK(directPlay.size() == 3);
  CHECK(directPlay.at(2).toObject().value(QStringLiteral("Type")).toString() ==
        QStringLiteral("Video"));
  CHECK(profile.value(QStringLiteral("CodecProfiles")).toArray().isEmpty());
  const QJsonObject video =
      profile.value(QStringLiteral("TranscodingProfiles")).toArray().at(1).toObject();
  CHECK(video.value(QStringLiteral("Protocol")).toString() == QStringLiteral("hls"));
  CHECK(video.value(QStringLiteral("VideoCodec")).toString() ==
        QStringLiteral("h264,mpeg4,mpeg2video"));
  CHECK(video.value(QStringLiteral("MaxAudioChannels")).toString() == QStringLiteral("6"));
}

TEST_CASE("Device profile options narrow direct play as in the 2.x client") {
  DeviceProfileOptions options;
  options.alwaysForceTranscode = true;
  options.forceTranscodeHevc = true;
  options.forceTranscode4k = true;
  options.allowTranscodeToHevc = true;
  options.preferTranscodeToHevc = true;
  options.maxTranscodeAudioChannels = 2;
  const QJsonObject profile = buildDeviceProfile(options);
  CHECK(profile.value(QStringLiteral("DirectPlayProfiles")).toArray().size() == 2);
  const QJsonArray codecs = profile.value(QStringLiteral("CodecProfiles")).toArray();
  REQUIRE(codecs.size() == 3);
  CHECK(codecs.at(0).toObject().value(QStringLiteral("Codec")).toString() ==
        QStringLiteral("hevc"));
  CHECK(codecs.at(1).toObject().value(QStringLiteral("Codec")).toString() ==
        QStringLiteral("h265"));
  CHECK(codecs.at(2).toObject().value(QStringLiteral("Conditions")).toArray().size() == 2);
  const QJsonObject video =
      profile.value(QStringLiteral("TranscodingProfiles")).toArray().at(1).toObject();
  CHECK(video.value(QStringLiteral("VideoCodec")).toString() ==
        QStringLiteral("h265,hevc,h264,mpeg4,mpeg2video"));
  CHECK(video.value(QStringLiteral("MaxAudioChannels")).toString() == QStringLiteral("2"));
}
