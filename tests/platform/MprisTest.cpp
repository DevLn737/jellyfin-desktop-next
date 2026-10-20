#include "../support/DoctestQt.h"
#include "PrivateBus.h"
#include "Wait.h"

#include <jfd/platform/DBusServices.h>

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>

using namespace jfd::platform;
using jfd::test::PrivateBus;
using jfd::test::spinUntil;

namespace {
  const QString Service = QStringLiteral("org.mpris.MediaPlayer2.jellyfin_test");
  const QString Path = QStringLiteral("/org/mpris/MediaPlayer2");
  const QString Player = QStringLiteral("org.mpris.MediaPlayer2.Player");

  // The service runs on this thread too, so calls are asynchronous and the
  // test spins the event loop until the reply arrives.
  QDBusMessage call(QDBusConnection& bus, const QString& interface, const QString& method,
                    const QVariantList& arguments = {}) {
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Path, interface, method);
    message.setArguments(arguments);
    QDBusPendingCallWatcher watcher(bus.asyncCall(message));
    spinUntil([&] { return watcher.isFinished(); });
    return watcher.reply();
  }

  QVariant property(QDBusConnection& bus, const QString& interface, const QString& name) {
    const QDBusMessage reply = call(bus, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
                                    {interface, name});
    if (reply.type() != QDBusMessage::ReplyMessage) return {};
    return reply.arguments().value(0).value<QDBusVariant>().variant();
  }

  QVariantMap metadata(QDBusConnection& bus) {
    QVariantMap map;
    property(bus, Player, QStringLiteral("Metadata")).value<QDBusArgument>() >> map;
    return map;
  }

  // Records Seeked and PropertiesChanged signals on the client side.
  class SignalLog final : public QObject {
    Q_OBJECT
  public:
    QList<qlonglong> seeked;
    QList<QStringList> changed;
  public Q_SLOTS:
    void onSeeked(qlonglong position) { seeked.append(position); }
    void onChanged(const QString&, const QVariantMap& properties, const QStringList&) {
      changed.append(properties.keys());
    }
  };

  MediaControlsState playing() {
    MediaControlsState state;
    state.status = MediaControlsState::Status::Playing;
    state.title = QStringLiteral("Серия 2");
    state.subtitle = QStringLiteral("Сериал · S01E02");
    state.artUrl = QUrl(QStringLiteral("https://media.example.org/Items/abc/Images/Primary?tag=1"));
    state.durationMs = 1'440'000;
    state.positionMs = 60'000;
    state.canSeek = true;
    state.canGoNext = true;
    return state;
  }
} // namespace

TEST_CASE("MPRIS shows what plays and sends commands back") {
  PrivateBus bus;
  if (!bus.isRunning()) {
    MESSAGE("dbus-daemon is not installed; skipped");
    return;
  }
  auto controls = createMprisMediaControls(bus.connect(QStringLiteral("app")), QStringLiteral("jellyfin_test"),
                                           QStringLiteral("Jellyfin Desktop Next"),
                                           QStringLiteral("io.github.devln737.JellyfinDesktopNext"));
  REQUIRE(controls->isAvailable());
  QDBusConnection client = bus.connect(QStringLiteral("client"));
  SignalLog log;
  REQUIRE(client.connect(Service, Path, Player, QStringLiteral("Seeked"), &log, SLOT(onSeeked(qlonglong))));
  REQUIRE(client.connect(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                         QStringLiteral("PropertiesChanged"), &log,
                         SLOT(onChanged(QString, QVariantMap, QStringList))));

  CHECK(property(client, QStringLiteral("org.mpris.MediaPlayer2"), QStringLiteral("Identity")).toString() ==
        QStringLiteral("Jellyfin Desktop Next"));
  CHECK(property(client, QStringLiteral("org.mpris.MediaPlayer2"), QStringLiteral("DesktopEntry")).toString() ==
        QStringLiteral("io.github.devln737.JellyfinDesktopNext"));
  CHECK(property(client, Player, QStringLiteral("PlaybackStatus")).toString() == QStringLiteral("Stopped"));
  CHECK_FALSE(property(client, Player, QStringLiteral("CanPlay")).toBool());

  controls->setState(playing());
  CHECK(property(client, Player, QStringLiteral("PlaybackStatus")).toString() == QStringLiteral("Playing"));
  CHECK(property(client, Player, QStringLiteral("CanSeek")).toBool());
  CHECK(property(client, Player, QStringLiteral("CanGoNext")).toBool());
  CHECK_FALSE(property(client, Player, QStringLiteral("CanGoPrevious")).toBool());
  const QVariantMap meta = metadata(client);
  CHECK(meta.value(QStringLiteral("xesam:title")).toString() == QStringLiteral("Серия 2"));
  CHECK(meta.value(QStringLiteral("mpris:length")).toLongLong() == 1'440'000'000LL);
  CHECK(meta.value(QStringLiteral("mpris:artUrl")).toString().startsWith(QStringLiteral("https://media.example.org/")));
  const qlonglong position = property(client, Player, QStringLiteral("Position")).toLongLong();
  CHECK(position >= 60'000'000LL);
  CHECK(position < 62'000'000LL);
  REQUIRE(spinUntil([&] { return !log.changed.isEmpty() && !log.seeked.isEmpty(); }));
  CHECK(log.changed.last().contains(QStringLiteral("PlaybackStatus")));
  CHECK(log.changed.last().contains(QStringLiteral("Metadata")));
  CHECK(log.seeked.last() == 60'000'000LL);

  QList<MediaCommand> commands;
  QList<qint64> seeks;
  QObject::connect(controls.get(), &MediaControls::commandRequested, [&](MediaCommand c) { commands.append(c); });
  QObject::connect(controls.get(), &MediaControls::seekRequested, [&](qint64 ms) { seeks.append(ms); });
  call(client, Player, QStringLiteral("PlayPause"));
  call(client, Player, QStringLiteral("Next"));
  // Previous is not possible here and is ignored.
  call(client, Player, QStringLiteral("Previous"));
  call(client, Player, QStringLiteral("Pause"));
  call(client, Player, QStringLiteral("SetPosition"),
       {QVariant::fromValue(QDBusObjectPath(QStringLiteral("/io/github/devln737/JellyfinDesktopNext/track"))),
        qlonglong(300'000'000)});
  // Another track id and positions past the end are ignored.
  call(client, Player, QStringLiteral("SetPosition"),
       {QVariant::fromValue(QDBusObjectPath(QStringLiteral("/other"))), qlonglong(1'000'000)});
  call(client, Player, QStringLiteral("SetPosition"),
       {QVariant::fromValue(QDBusObjectPath(QStringLiteral("/io/github/devln737/JellyfinDesktopNext/track"))),
        qlonglong(9'000'000'000LL)});
  call(client, Player, QStringLiteral("Seek"), {qlonglong(-120'000'000)});
  CHECK(commands == QList<MediaCommand>{MediaCommand::PlayPause, MediaCommand::Next, MediaCommand::Pause});
  REQUIRE(seeks.size() == 2);
  CHECK(seeks[0] == 300'000);
  // 60 s minus 120 s clamps to the start.
  CHECK(seeks[1] == 0);

  // A paused state that jumps announces a seek; one that continues does not.
  log.seeked.clear();
  MediaControlsState paused = playing();
  paused.status = MediaControlsState::Status::Paused;
  paused.positionMs = 600'000;
  controls->setState(paused);
  REQUIRE(spinUntil([&] { return !log.seeked.isEmpty(); }));
  CHECK(log.seeked.last() == 600'000'000LL);
  CHECK(property(client, Player, QStringLiteral("PlaybackStatus")).toString() == QStringLiteral("Paused"));
  log.seeked.clear();
  paused.positionMs = 600'200;
  controls->setState(paused);
  spinUntil([] { return false; }, 200);
  CHECK(log.seeked.isEmpty());

  bool raised = false;
  QObject::connect(controls.get(), &MediaControls::raiseRequested, [&] { raised = true; });
  call(client, QStringLiteral("org.mpris.MediaPlayer2"), QStringLiteral("Raise"));
  CHECK(raised);

  controls.reset();
  const QDBusMessage gone = call(client, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
                                 {Player, QStringLiteral("PlaybackStatus")});
  CHECK(gone.type() == QDBusMessage::ErrorMessage);
}

#include "MprisTest.moc"
