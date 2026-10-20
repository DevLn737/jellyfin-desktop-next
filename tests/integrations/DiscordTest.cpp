#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/integrations/DiscordActivity.h>
#include <jfd/integrations/DiscordIpc.h>
#include <jfd/integrations/DiscordPresence.h>

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUuid>
#include <QtEndian>

#include <memory>

using namespace jfd::integrations;
using jfd::test::waitFor;

namespace {
  const QString AppId = QStringLiteral("1557831977250463766");
  const QUrl Server(QStringLiteral("https://user:password@media.example.org/jellyfin?api_key=secret#private"));

  QByteArray frame(quint32 opcode, const QByteArray& payload) {
    QByteArray bytes(8, '\0');
    qToLittleEndian<quint32>(opcode, bytes.data());
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), bytes.data() + 4);
    return bytes + payload;
  }

  QByteArray jsonFrame(const QJsonObject& payload) {
    return frame(1, QJsonDocument(payload).toJson(QJsonDocument::Compact));
  }

  // The fake Discord and the client share an event loop; never block in waitForReadyRead.
  QJsonObject readJson(QLocalSocket* socket) {
    if (!waitFor([&] { return socket->bytesAvailable() >= 8; }, 10000)) return {};
    const QByteArray header = socket->peek(8);
    const auto size = qFromLittleEndian<quint32>(header.constData() + 4);
    if (size > 65536) return {};
    if (!waitFor([&] { return socket->bytesAvailable() >= 8 + size; }, 10000)) return {};
    socket->read(8);
    return QJsonDocument::fromJson(socket->read(size)).object();
  }

  void acknowledge(QLocalSocket* socket, const QJsonObject& request) {
    socket->write(jsonFrame({{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
                             {QStringLiteral("nonce"), request.value(QStringLiteral("nonce"))},
                             {QStringLiteral("data"), QJsonObject()}}));
    socket->flush();
  }

  void ready(QLocalSocket* socket) {
    socket->write(jsonFrame({{QStringLiteral("cmd"), QStringLiteral("DISPATCH")}, {QStringLiteral("evt"), QStringLiteral("READY")}}));
    socket->flush();
  }

  void pause(int ms) {
    waitFor([] { return false; }, ms);
  }

  QString uniquePrefix() {
    return QStringLiteral("jfd-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('-');
  }

  PresenceInfo movie() {
    PresenceInfo info;
    info.kind = PresenceInfo::Kind::Movie;
    info.title = QStringLiteral("Arrival");
    info.year = 2016;
    info.playing = true;
    info.positionMs = 125000;
    info.poster = publicPosterUrl(Server, QStringLiteral("abc123"), QStringLiteral("etag123"));
    return info;
  }

  QString state(const QJsonObject& activity) { return activity.value(QStringLiteral("state")).toString(); }
  double start(const QJsonObject& activity) {
    return activity.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toDouble();
  }
} // namespace

TEST_CASE("A movie shows its title, year, poster and an elapsed timer") {
  const PresenceInfo info = movie();
  const QJsonObject activity = buildActivity(info, presenceStart(0, info.positionMs, true, 1800000000));
  CHECK(activity.value(QStringLiteral("type")).toInt() == 3);
  CHECK(activity.value(QStringLiteral("details")).toString() == QStringLiteral("Arrival"));
  CHECK(state(activity) == QStringLiteral("Movie · 2016"));
  CHECK(start(activity) == 1799999875.0);
  CHECK_FALSE(activity.value(QStringLiteral("timestamps")).toObject().contains(QStringLiteral("end")));
  const QJsonObject assets = activity.value(QStringLiteral("assets")).toObject();
  CHECK(assets.value(QStringLiteral("large_image")).toString() ==
        QStringLiteral("https://media.example.org/jellyfin/Items/abc123/Images/Primary?tag=etag123&maxHeight=512&quality=90"));
  const QByteArray json = QJsonDocument(activity).toJson();
  CHECK_FALSE(json.contains("secret"));
  CHECK_FALSE(json.contains("password"));
  CHECK_FALSE(json.contains("private"));
}

TEST_CASE("A full runtime gives Discord's time bar an end") {
  PresenceInfo info = movie();
  info.durationMs = 10'140'000;
  const QJsonObject activity = buildActivity(info, 1800000000 - 125);
  CHECK(activity.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("end")).toDouble() ==
        1800000000.0 - 125 + 10140);
}

TEST_CASE("Paused and loading states show the position; the timer anchor stays") {
  PresenceInfo info = movie();
  info.playing = false;
  const qint64 anchor = presenceStart(0, info.positionMs, true, 1800000000);
  CHECK(presenceStart(anchor, info.positionMs, false, 1800000300) == anchor);
  QJsonObject activity = buildActivity(info, anchor);
  CHECK(state(activity) == QStringLiteral("Movie · 2016 · Paused · 2:05"));
  info.positionMs = 3'600'000;
  info.buffering = true;
  activity = buildActivity(info, anchor);
  CHECK(state(activity) == QStringLiteral("Movie · 2016 · Loading · 1:00:00"));
  // Playing again rebases.
  CHECK(presenceStart(anchor, 3'600'000, true, 1800000400) == 1800000400 - 3600);
}

TEST_CASE("Episodes show the series, season and episode, and the group") {
  PresenceInfo info;
  info.kind = PresenceInfo::Kind::Episode;
  info.title = QStringLiteral("Pilot");
  info.seriesName = QStringLiteral("A Series");
  info.season = 0;
  info.episode = 1;
  info.episodeEnd = 2;
  info.playing = true;
  QJsonObject activity = buildActivity(info, 1800000000);
  CHECK(activity.value(QStringLiteral("details")).toString() == QStringLiteral("A Series"));
  CHECK(state(activity) == QStringLiteral("Series · S00 E01–02"));
  info.groupSize = 3;
  activity = buildActivity(info, 1800000000);
  CHECK(state(activity) == QStringLiteral("Series · S00 E01–02 · Watching together (3)"));
  // Alone in a group is not "together".
  info.groupSize = 1;
  info.season.reset();
  info.episode.reset();
  CHECK(state(buildActivity(info, 1800000000)) == QStringLiteral("Series"));
}

TEST_CASE("Nothing is shown without a title; private posters and long titles are handled") {
  PresenceInfo info = movie();
  info.title.clear();
  CHECK(buildActivity(info, 1).isEmpty());
  for (const char* url : {"http://media.example.org", "https://192.168.0.1", "https://localhost", "https://server.local",
                          "https://[::1]", "https://nas.lan", "https://box.home.arpa/jellyfin"})
    CHECK_MESSAGE(publicPosterUrl(QUrl(QString::fromLatin1(url)), QStringLiteral("a"), QStringLiteral("b")).isEmpty(), url);
  CHECK(publicPosterUrl(Server, QStringLiteral("a/../b"), QStringLiteral("t")).isEmpty());
  info = movie();
  info.poster = QUrl();
  CHECK(buildActivity(info, 1)
            .value(QStringLiteral("assets"))
            .toObject()
            .value(QStringLiteral("large_image"))
            .toString()
            .endsWith(QStringLiteral("/icon.png")));
  info.title = QString::fromUtf8("🎬").repeated(100);
  const QString title = buildActivity(info, 1).value(QStringLiteral("details")).toString();
  CHECK(title.toUtf8().size() <= 128);
  CHECK_FALSE(title.contains(QChar::ReplacementCharacter));
}

TEST_CASE("IPC: handshake, split frames, ping, coalesced updates and clearing") {
  const QString prefix = uniquePrefix();
  QLocalServer server;
  REQUIRE(server.listen(prefix + QLatin1Char('0')));
  DiscordIpc ipc({prefix + QLatin1Char('0')});
  const QJsonObject watching = buildActivity(movie(), 1799999990);
  ipc.setActivity(watching);
  ipc.configure(true, AppId);
  REQUIRE(waitFor([&] { return server.hasPendingConnections(); }));
  std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection());
  CHECK(readJson(peer.get()).value(QStringLiteral("client_id")).toString() == AppId);

  const QByteArray readyFrame =
      jsonFrame({{QStringLiteral("cmd"), QStringLiteral("DISPATCH")}, {QStringLiteral("evt"), QStringLiteral("READY")}});
  peer->write(readyFrame.left(5));
  peer->flush();
  pause(30);
  CHECK(peer->bytesAvailable() == 0);
  peer->write(readyFrame.mid(5));
  peer->flush();
  const QJsonObject first = readJson(peer.get());
  CHECK(first.value(QStringLiteral("cmd")).toString() == QStringLiteral("SET_ACTIVITY"));
  CHECK(first.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject() == watching);
  CHECK(first.value(QStringLiteral("args")).toObject().value(QStringLiteral("pid")).toDouble() > 0);
  acknowledge(peer.get(), first);
  pause(30);
  // The same activity again is not sent.
  ipc.setActivity(watching);
  pause(30);
  CHECK(peer->bytesAvailable() == 0);

  peer->write(frame(3, "ping"));
  peer->flush();
  REQUIRE(waitFor([&] { return peer->bytesAvailable() == 12; }));
  CHECK(peer->readAll() == frame(4, "ping"));

  PresenceInfo paused = movie();
  paused.playing = false;
  const QJsonObject pausedActivity = buildActivity(paused, 1799999990);
  ipc.setActivity(pausedActivity);
  const QJsonObject pauseRequest = readJson(peer.get());
  CHECK(pauseRequest.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject() == pausedActivity);
  // A stop while an update is in flight wins as soon as the reply arrives.
  ipc.setActivity({});
  acknowledge(peer.get(), pauseRequest);
  const QJsonObject clear = readJson(peer.get());
  CHECK(clear.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).isNull());
  acknowledge(peer.get(), clear);
  ipc.configure(false, AppId);
  CHECK(waitFor([&] { return peer->state() == QLocalSocket::UnconnectedState; }));
}

TEST_CASE("IPC: after a reconnect the latest activity is sent") {
  const QString prefix = uniquePrefix();
  QLocalServer firstServer;
  QLocalServer nextServer;
  REQUIRE(firstServer.listen(prefix + QLatin1Char('0')));
  REQUIRE(nextServer.listen(prefix + QLatin1Char('1')));
  DiscordIpc ipc({prefix + QLatin1Char('0'), prefix + QLatin1Char('1')});
  ipc.configure(true, AppId);
  REQUIRE(waitFor([&] { return firstServer.hasPendingConnections(); }));
  std::unique_ptr<QLocalSocket> first(firstServer.nextPendingConnection());
  CHECK_FALSE(readJson(first.get()).isEmpty());
  ready(first.get());
  acknowledge(first.get(), readJson(first.get()));
  first->abort();
  const QJsonObject latest = buildActivity(movie(), 1800000000);
  ipc.setActivity(latest);
  REQUIRE(waitFor([&] { return nextServer.hasPendingConnections(); }, 20000));
  std::unique_ptr<QLocalSocket> second(nextServer.nextPendingConnection());
  CHECK(readJson(second.get()).value(QStringLiteral("client_id")).toString() == AppId);
  ready(second.get());
  CHECK(readJson(second.get()).value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject() == latest);
  ipc.shutdown();
}

TEST_CASE("IPC: an invalid id never connects; a malformed frame disconnects") {
  const QString prefix = uniquePrefix();
  QLocalServer server;
  REQUIRE(server.listen(prefix + QLatin1Char('0')));
  DiscordIpc ipc({prefix + QLatin1Char('0')});
  ipc.configure(true, QStringLiteral("not-an-id"));
  pause(50);
  CHECK_FALSE(server.hasPendingConnections());
  ipc.configure(true, AppId);
  REQUIRE(waitFor([&] { return server.hasPendingConnections(); }));
  std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection());
  CHECK_FALSE(readJson(peer.get()).isEmpty());
  QByteArray invalid(8, '\0');
  qToLittleEndian<quint32>(1, invalid.data());
  qToLittleEndian<quint32>(65537, invalid.data() + 4);
  peer->write(invalid);
  peer->flush();
  CHECK(waitFor([&] { return peer->state() == QLocalSocket::UnconnectedState; }));
  ipc.shutdown();
}

TEST_CASE("Presence keeps the timer through pauses and rebases on seeks and new items") {
  const QString prefix = uniquePrefix();
  QLocalServer server;
  REQUIRE(server.listen(prefix + QLatin1Char('0')));
  DiscordPresence presence({prefix + QLatin1Char('0')});
  qint64 now = 1800000000;
  presence.setClock([&] { return now; });
  presence.configure(true, AppId);
  REQUIRE(waitFor([&] { return server.hasPendingConnections(); }));
  std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection());
  CHECK_FALSE(readJson(peer.get()).isEmpty());
  ready(peer.get());
  auto next = [&] {
    const QJsonObject request = readJson(peer.get());
    acknowledge(peer.get(), request);
    return request.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity"));
  };
  // Nothing plays yet: the card is cleared.
  CHECK(next().isNull());

  PresenceInfo info = movie();
  info.positionMs = 10'000;
  presence.update(info);
  QElapsedTimer played;
  played.start();
  const QJsonObject playing = next().toObject();
  CHECK(start(playing) == double(now - 10));

  // Paused a while later: the anchor stays.
  now += 300;
  info.playing = false;
  // Updates are sent at most every five seconds, so real time has passed.
  info.positionMs += played.elapsed();
  presence.update(info);
  const QJsonObject paused = next().toObject();
  CHECK(qint64(start(paused)) == qint64(start(playing)));
  CHECK(state(paused).contains(QStringLiteral("Paused · 0:1")));

  // A seek while paused rebases.
  now += 10;
  info.positionMs = 600'000;
  presence.update(info);
  CHECK(start(next().toObject()) == double(now - 600));

  presence.update(std::nullopt);
  CHECK(next().isNull());
}
