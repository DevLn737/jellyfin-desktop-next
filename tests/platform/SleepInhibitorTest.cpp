#include "../support/DoctestQt.h"
#include "PrivateBus.h"
#include "Wait.h"

#include <jfd/platform/DBusServices.h>

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QVariantMap>

using namespace jfd::platform;
using jfd::test::PrivateBus;
using jfd::test::spinUntil;

namespace {

  // org.freedesktop.portal.Request for one inhibition.
  class FakeRequest final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
  public:
    int closed = 0;
  public Q_SLOTS:
    void Close() { ++closed; }
  };

  class FakePortal final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Inhibit")
  public:
    explicit FakePortal(QDBusConnection bus) : m_bus(std::move(bus)) {}
    uint lastFlags = 0;
    QString lastReason;
    int inhibits = 0;
    FakeRequest request;

  public Q_SLOTS:
    QDBusObjectPath Inhibit(const QString&, uint flags, const QVariantMap& options) {
      ++inhibits;
      lastFlags = flags;
      lastReason = options.value(QStringLiteral("reason")).toString();
      const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/1_1/t%1").arg(inhibits);
      m_bus.registerObject(path, &request, QDBusConnection::ExportAllSlots);
      return QDBusObjectPath(path);
    }

  private:
    QDBusConnection m_bus;
  };

  class FakeScreenSaver final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")
  public:
    QList<uint> active;
    QString lastApplication;
    QString lastReason;

  public Q_SLOTS:
    uint Inhibit(const QString& application, const QString& reason) {
      lastApplication = application;
      lastReason = reason;
      active.append(m_next);
      return m_next++;
    }
    void UnInhibit(uint cookie) { active.removeAll(cookie); }

  private:
    uint m_next = 41;
  };

  bool registerService(QDBusConnection& bus, const QString& name, const QString& path, QObject* object) {
    return bus.registerObject(path, object, QDBusConnection::ExportAllSlots) && bus.registerService(name);
  }

} // namespace

TEST_CASE("The portal keeps the system awake while playing and releases on pause") {
  PrivateBus bus;
  if (!bus.isRunning()) {
    MESSAGE("dbus-daemon is not installed; skipped");
    return;
  }
  QDBusConnection service = bus.connect(QStringLiteral("portal"));
  FakePortal portal(service);
  REQUIRE(registerService(service, QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"), &portal));

  auto inhibitor = createDBusSleepInhibitor(bus.connect(QStringLiteral("client")));
  inhibitor->setInhibited(true, QStringLiteral("Playing video"));
  REQUIRE(spinUntil([&] { return inhibitor->isInhibited(); }));
  CHECK(inhibitor->backend() == SleepInhibitor::Backend::Portal);
  CHECK(portal.inhibits == 1);
  CHECK(portal.lastFlags == (4u | 8u));
  CHECK(portal.lastReason == QStringLiteral("Playing video"));

  // Repeated calls while held do nothing.
  inhibitor->setInhibited(true, QStringLiteral("Playing video"));
  inhibitor->setInhibited(false, {});
  CHECK_FALSE(inhibitor->isInhibited());
  REQUIRE(spinUntil([&] { return portal.request.closed == 1; }));
  CHECK(portal.inhibits == 1);

  // Play, pause, play quickly: one inhibition left at the end.
  inhibitor->setInhibited(true, QStringLiteral("Playing video"));
  inhibitor->setInhibited(false, {});
  inhibitor->setInhibited(true, QStringLiteral("Playing video"));
  REQUIRE(spinUntil([&] { return inhibitor->isInhibited(); }));
  spinUntil([] { return false; }, 200);
  CHECK(portal.inhibits == 2);
  CHECK(inhibitor->isInhibited());

  // Destroying the inhibitor releases what it holds.
  inhibitor.reset();
  CHECK(spinUntil([&] { return portal.request.closed == 2; }));
}

TEST_CASE("Without a portal the screen saver service is used") {
  PrivateBus bus;
  if (!bus.isRunning()) {
    MESSAGE("dbus-daemon is not installed; skipped");
    return;
  }
  QDBusConnection service = bus.connect(QStringLiteral("screensaver"));
  FakeScreenSaver screenSaver;
  REQUIRE(registerService(service, QStringLiteral("org.freedesktop.ScreenSaver"),
                          QStringLiteral("/org/freedesktop/ScreenSaver"), &screenSaver));

  auto inhibitor = createDBusSleepInhibitor(bus.connect(QStringLiteral("client")));
  int changes = 0;
  QObject::connect(inhibitor.get(), &SleepInhibitor::activeChanged, [&](bool) { ++changes; });
  inhibitor->setInhibited(true, QStringLiteral("Playing video"));
  REQUIRE(spinUntil([&] { return inhibitor->isInhibited(); }));
  CHECK(inhibitor->backend() == SleepInhibitor::Backend::ScreenSaver);
  CHECK(screenSaver.active == QList<uint>{41});
  CHECK(screenSaver.lastReason == QStringLiteral("Playing video"));

  inhibitor->setInhibited(false, {});
  REQUIRE(spinUntil([&] { return screenSaver.active.isEmpty(); }));
  CHECK(changes == 2);
}

TEST_CASE("With neither service the inhibitor stays released") {
  PrivateBus bus;
  if (!bus.isRunning()) {
    MESSAGE("dbus-daemon is not installed; skipped");
    return;
  }
  auto inhibitor = createDBusSleepInhibitor(bus.connect(QStringLiteral("client")));
  inhibitor->setInhibited(true, QStringLiteral("Playing video"));
  spinUntil([] { return false; }, 500);
  CHECK_FALSE(inhibitor->isInhibited());
  CHECK(inhibitor->backend() == SleepInhibitor::Backend::None);
  inhibitor->setInhibited(false, {});
}

#include "SleepInhibitorTest.moc"
