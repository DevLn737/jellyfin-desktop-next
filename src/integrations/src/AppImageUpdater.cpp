#include <jfd/integrations/AppImageUpdater.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

namespace jfd::integrations {

  namespace {
    // The new version has this long to confirm, in tenths of a second.
    constexpr int ConfirmTenths = 600;

    // Variables the AppImage runtime and its AppRun set for this process;
    // the next AppImage sets its own.
    QProcessEnvironment cleanEnvironment() {
      QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
      const QString appDir = environment.value(QStringLiteral("APPDIR"));
      for (const QString& key : environment.keys()) {
        if (!appDir.isEmpty() && environment.value(key).contains(appDir)) environment.remove(key);
      }
      for (const char* key : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"}) environment.remove(QString::fromLatin1(key));
      return environment;
    }
  } // namespace

  QString AppImageUpdater::assetName(const QString& version) {
    return QStringLiteral("jellyfin-desktop-next-%1-x86_64.AppImage").arg(version);
  }

  QString AppImageUpdater::watchdogScript() {
    // $1 pid of the old version, $2 the AppImage, $3 the old file kept,
    // $4 the marker, $5 tenths of a second the new version has to confirm.
    return QStringLiteral(R"(pid=$1 app=$2 old=$3 marker=$4 limit=$5
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
rm -f "$marker.ok" "$marker.exited" "$marker.pid"
( "$app" & echo $! > "$marker.pid"; wait $!; : > "$marker.exited" ) &
i=0
while [ "$i" -lt "$limit" ] && [ ! -e "$marker.ok" ] && [ ! -e "$marker.exited" ]; do
  sleep 0.1; i=$((i + 1))
done
if [ -e "$marker.ok" ]; then rm -f "$old" "$marker.ok" "$marker.pid"; exit 0; fi
child=$(cat "$marker.pid" 2>/dev/null)
if [ -n "$child" ]; then kill "$child" 2>/dev/null; sleep 1; kill -9 "$child" 2>/dev/null; fi
mv -f "$old" "$app" && : > "$marker.failed"
rm -f "$marker.pid" "$marker.exited"
exec "$app"
)");
  }

  AppImageUpdater::AppImageUpdater(QNetworkAccessManager* network, QString appImage, QString stateDirectory,
                                   std::optional<MinisignPublicKey> key, QObject* parent)
      : QObject(parent), m_network(network), m_appImage(std::move(appImage)),
        m_stateDirectory(std::move(stateDirectory)), m_key(key), m_download(network, this) {}

  bool AppImageUpdater::supported() const {
    if (m_appImage.isEmpty() || !m_key) return false;
    const QFileInfo file(m_appImage);
    return file.isFile() && QFileInfo(file.absolutePath()).isWritable();
  }

  QString AppImageUpdater::markerPath() const {
    return QDir(m_stateDirectory).filePath(QStringLiteral("update-pending.json"));
  }

  void AppImageUpdater::prepare(const Release& release) {
    if (!supported() || !m_preparing.isEmpty() || m_readyVersion == release.version) return;
    const QString name = assetName(release.version);
    const ReleaseAsset* file = SignedDownload::findAsset(release, name);
    const ReleaseAsset* signature = SignedDownload::findAsset(release, name + QStringLiteral(".minisig"));
    if (file == nullptr || signature == nullptr) {
      Q_EMIT failed(QStringLiteral("release %1 has no signed AppImage").arg(release.version));
      return;
    }
    m_preparing = release.version;
    const QString part = m_appImage + QStringLiteral(".new.part");
    m_download.start(*file, *signature, part, name, *m_key, [this, part](const QString& error) {
      if (!error.isEmpty()) {
        m_preparing.clear();
        Q_EMIT failed(error);
        return;
      }
      const QString next = m_appImage + QStringLiteral(".new");
      QFile::remove(next);
      if (!QFile::rename(part, next) ||
          !QFile::setPermissions(next, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                           QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                           QFileDevice::ReadOther | QFileDevice::ExeOther)) {
        QFile::remove(part);
        m_preparing.clear();
        Q_EMIT failed(QStringLiteral("cannot keep the download"));
        return;
      }
      m_readyVersion = m_preparing;
      m_preparing.clear();
      Q_EMIT ready(m_readyVersion);
    });
  }

  bool AppImageUpdater::apply(const QString& route) {
    if (m_readyVersion.isEmpty()) return false;
    const QString next = m_appImage + QStringLiteral(".new");
    const QString old = m_appImage + QStringLiteral(".old");
    if (!QDir().mkpath(m_stateDirectory)) return false;
    QFile marker(markerPath());
    const QJsonObject state{{QStringLiteral("version"), m_readyVersion},
                            {QStringLiteral("previous"), QCoreApplication::applicationVersion()},
                            {QStringLiteral("route"), route}};
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        marker.write(QJsonDocument(state).toJson()) < 0 || !marker.flush())
      return false;
    marker.close();
    for (const char* suffix : {".ok", ".failed", ".exited", ".pid"})
      QFile::remove(markerPath() + QString::fromLatin1(suffix));

    QFile::remove(old);
    if (!QFile::rename(m_appImage, old)) {
      QFile::remove(markerPath());
      return false;
    }
    if (!QFile::rename(next, m_appImage)) {
      QFile::rename(old, m_appImage);
      QFile::remove(markerPath());
      return false;
    }
    QProcess watchdog;
    watchdog.setProgram(QStringLiteral("/bin/sh"));
    watchdog.setArguments({QStringLiteral("-c"), watchdogScript(), QStringLiteral("sh"),
                           QString::number(QCoreApplication::applicationPid()), m_appImage, old, markerPath(),
                           QString::number(ConfirmTenths)});
    watchdog.setProcessEnvironment(cleanEnvironment());
    watchdog.setWorkingDirectory(QDir::homePath());
    watchdog.setStandardOutputFile(QProcess::nullDevice());
    watchdog.setStandardErrorFile(QProcess::nullDevice());
    if (!watchdog.startDetached()) {
      QFile::rename(m_appImage, next);
      QFile::rename(old, m_appImage);
      QFile::remove(markerPath());
      return false;
    }
    return true;
  }

  AppImageUpdater::StartState AppImageUpdater::confirmStart(const QString& currentVersion) {
    StartState result;
    const QString path = markerPath();
    QFile marker(path);
    if (!marker.open(QIODevice::ReadOnly)) return result;
    const QJsonObject state = QJsonDocument::fromJson(marker.readAll()).object();
    marker.close();
    const QString version = state.value(QStringLiteral("version")).toString();
    if (QFile::exists(path + QStringLiteral(".failed"))) {
      result = {StartState::Kind::RolledBack, version, {}};
    } else if (version == currentVersion) {
      // The watchdog waits for this file.
      QFile ok(path + QStringLiteral(".ok"));
      if (ok.open(QIODevice::WriteOnly)) ok.close();
      result = {StartState::Kind::Updated, version, state.value(QStringLiteral("route")).toString()};
    }
    QFile::remove(path);
    for (const char* suffix : {".failed", ".exited"}) QFile::remove(path + QString::fromLatin1(suffix));
    return result;
  }

  void AppImageUpdater::handleUrl(const QUrl& url) {
    if (url.scheme() == urlScheme()) Q_EMIT restartRequested();
  }

} // namespace jfd::integrations
