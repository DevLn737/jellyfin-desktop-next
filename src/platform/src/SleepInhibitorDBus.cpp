#include <jfd/platform/DBusServices.h>

#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

namespace jfd::platform {

  namespace {
    const QString PortalService = QStringLiteral("org.freedesktop.portal.Desktop");
    const QString PortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
    const QString PortalInhibit = QStringLiteral("org.freedesktop.portal.Inhibit");
    const QString PortalRequest = QStringLiteral("org.freedesktop.portal.Request");
    const QString ScreenSaverService = QStringLiteral("org.freedesktop.ScreenSaver");
    const QString ScreenSaverPath = QStringLiteral("/org/freedesktop/ScreenSaver");
    const QString ScreenSaverInterface = QStringLiteral("org.freedesktop.ScreenSaver");
    // org.freedesktop.portal.Inhibit flags: 4 suspend, 8 idle (screen blanking).
    constexpr uint InhibitSuspendAndIdle = 4 | 8;

    // Requests are asynchronous; the wanted state can change while one is in
    // flight, so each reply is reconciled with what is wanted now.
    class DBusSleepInhibitor final : public SleepInhibitor {
    public:
      DBusSleepInhibitor(const QDBusConnection& bus, QObject* parent)
          : SleepInhibitor(parent), m_bus(bus) {}

      ~DBusSleepInhibitor() override {
        // The bus releases inhibitions of a client that disconnects, but the
        // connection outlives this object; release explicitly.
        releaseHeld();
      }

      void setInhibited(bool inhibited, const QString& reason) override {
        m_wanted = inhibited;
        m_reason = reason;
        reconcile();
      }

    private:
      void reconcile() {
        if (m_pending) return;
        const bool held = !m_portalHandle.isEmpty() || m_screenSaverCookie != 0;
        if (m_wanted && !held)
          requestPortal();
        else if (!m_wanted && held)
          releaseHeld();
      }

      void requestPortal() {
        QDBusMessage call =
            QDBusMessage::createMethodCall(PortalService, PortalPath, PortalInhibit, QStringLiteral("Inhibit"));
        call << QString() << InhibitSuspendAndIdle
             << QVariantMap{{QStringLiteral("reason"), m_reason}};
        watch(m_bus.asyncCall(call), [this](QDBusPendingCallWatcher* watcher) {
          QDBusPendingReply<QDBusObjectPath> reply = *watcher;
          if (reply.isError()) {
            requestScreenSaver();
            return;
          }
          m_portalHandle = reply.value().path();
          setActive(true, Backend::Portal);
          reconcile();
        });
      }

      void requestScreenSaver() {
        QDBusMessage call = QDBusMessage::createMethodCall(ScreenSaverService, ScreenSaverPath,
                                                           ScreenSaverInterface, QStringLiteral("Inhibit"));
        call << QCoreApplication::applicationName() << m_reason;
        watch(m_bus.asyncCall(call), [this](QDBusPendingCallWatcher* watcher) {
          QDBusPendingReply<uint> reply = *watcher;
          if (reply.isError() || reply.value() == 0) {
            if (!m_warned)
              qWarning("Cannot keep the screen on: no desktop portal or screen saver service (%s)",
                       qPrintable(reply.error().message()));
            m_warned = true;
            // Not retried until the next change of state.
            m_wanted = false;
            return;
          }
          m_screenSaverCookie = reply.value();
          setActive(true, Backend::ScreenSaver);
          reconcile();
        });
      }

      void releaseHeld() {
        if (!m_portalHandle.isEmpty()) {
          m_bus.asyncCall(QDBusMessage::createMethodCall(PortalService, m_portalHandle, PortalRequest,
                                                         QStringLiteral("Close")));
          m_portalHandle.clear();
        }
        if (m_screenSaverCookie != 0) {
          QDBusMessage call = QDBusMessage::createMethodCall(
              ScreenSaverService, ScreenSaverPath, ScreenSaverInterface, QStringLiteral("UnInhibit"));
          call << m_screenSaverCookie;
          m_bus.asyncCall(call);
          m_screenSaverCookie = 0;
        }
        setActive(false, Backend::None);
      }

      template <class Done> void watch(const QDBusPendingCall& call, Done done) {
        m_pending = true;
        auto* watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, done](QDBusPendingCallWatcher* finished) {
                  m_pending = false;
                  done(finished);
                  finished->deleteLater();
                });
      }

      QDBusConnection m_bus;
      QString m_reason;
      QString m_portalHandle;
      uint m_screenSaverCookie = 0;
      bool m_wanted = false;
      bool m_pending = false;
      bool m_warned = false;
    };
  } // namespace

  std::unique_ptr<SleepInhibitor> createDBusSleepInhibitor(const QDBusConnection& bus,
                                                           QObject* parent) {
    return std::make_unique<DBusSleepInhibitor>(bus, parent);
  }

  std::unique_ptr<SleepInhibitor> createSystemSleepInhibitor(QObject* parent) {
    return createDBusSleepInhibitor(QDBusConnection::sessionBus(), parent);
  }

} // namespace jfd::platform
