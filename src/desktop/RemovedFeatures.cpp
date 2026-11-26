#include "AppServices.h"

#include <jfd/platform/CredentialStore.h>

#include <QLoggingCategory>
#include <QSettings>
#include <QUrl>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcRemoved, "jfd.app.settings")

    // The key the removed Seerr integration kept its session cookie under
    // in the system credential store, for the address the user typed.
    QString seerrSessionKey(const QString& address) {
      QString text = address.trimmed();
      if (text.isEmpty()) return {};
      if (!text.contains(QLatin1String("://"))) text.prepend(QLatin1String("http://"));
      const QUrl url(text, QUrl::StrictMode);
      if (!url.isValid() || url.host().isEmpty() ||
          (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return {};
      QString path = url.path();
      while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
      return QStringLiteral("seerr-session:%1://%2:%3%4")
          .arg(url.scheme(), url.host().toLower())
          .arg(url.port(url.scheme() == QLatin1String("https") ? 443 : 80))
          .arg(path);
    }
  } // namespace

  void forgetRemovedFeatures(const QString& settingsFile, bool isolatedProfile) {
    QSettings store(settingsFile, QSettings::IniFormat);
    if (!store.childGroups().contains(QLatin1String("seerr"))) return;
    const QString key = seerrSessionKey(store.value(QStringLiteral("seerr/address")).toString());
    if (!key.isEmpty() && !isolatedProfile) {
      const auto credentials = platform::createSystemCredentialStore();
      if (credentials->isAvailable()) credentials->remove(key);
    }
    store.remove(QStringLiteral("seerr"));
    store.sync();
    qCInfo(lcRemoved) << "removed the settings of the Seerr integration";
  }

} // namespace jfd::desktop
