#include <jfd/integrations/MsiUpdater.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>

namespace jfd::integrations {

  namespace {
    // Compares folders the way Windows does, whatever the separators.
    QString normalized(QString folder) {
      folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
      return QDir::cleanPath(folder).toLower();
    }

    QString scriptFile(const QString& stateDirectory) {
      return QDir(stateDirectory).filePath(QStringLiteral("apply-update.ps1"));
    }
  } // namespace

  QString MsiUpdater::assetName(const QString& version) {
    return QStringLiteral("jellyfin-desktop-next-%1-x64.msi").arg(version);
  }

  QString MsiUpdater::installedFolder() {
#ifdef Q_OS_WIN
    // Written by the MSI (packaging/windows/jellyfin-desktop-next.wxs).
    return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Jellyfin Desktop Next"), QSettings::NativeFormat)
        .value(QStringLiteral("InstallFolder"))
        .toString();
#else
    return {};
#endif
  }

  QString MsiUpdater::updateScript() {
    // Both outcomes start the program again: after a successful install it is
    // the new version, after a failed one Windows Installer has put the old
    // one back. The marker's .failed file tells the old version why it is
    // running again. msiexec exits with 0, or 3010 when a restart would be
    // needed (an install for one user has no files in use by the system).
    return QStringLiteral(R"PS(param([int]$ProcessId, [string]$Msi, [string]$Exe, [string]$Log, [string]$Marker)
$ErrorActionPreference = 'Stop'
Remove-Item -LiteralPath "$Marker.failed" -Force -ErrorAction SilentlyContinue
try { Wait-Process -Id $ProcessId -Timeout 120 } catch { }
$code = 1
try {
  $install = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', "`"$Msi`"", '/qb', '/norestart', '/l*v', "`"$Log`"") -Wait -PassThru
  $code = $install.ExitCode
} catch { }
if ($code -ne 0 -and $code -ne 3010) { New-Item -ItemType File -Path "$Marker.failed" -Force | Out-Null }
Remove-Item -LiteralPath $Msi -Force -ErrorAction SilentlyContinue
try { Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) } catch { }
)PS");
  }

  QStringList MsiUpdater::scriptArguments(const QString& scriptPath, qint64 pid, const QString& msi,
                                          const QString& executable, const QString& log, const QString& marker) {
    return {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
            QStringLiteral("-File"), scriptPath,
            QStringLiteral("-ProcessId"), QString::number(pid),
            QStringLiteral("-Msi"), msi,
            QStringLiteral("-Exe"), executable,
            QStringLiteral("-Log"), log,
            QStringLiteral("-Marker"), marker};
  }

  MsiUpdater::MsiUpdater(QNetworkAccessManager* network, QString executable, QString installFolder,
                         QString stateDirectory, std::optional<MinisignPublicKey> key, QObject* parent)
      : QObject(parent), m_executable(std::move(executable)), m_installFolder(std::move(installFolder)),
        m_stateDirectory(std::move(stateDirectory)), m_key(key), m_download(network, this) {}

  bool MsiUpdater::supported() const {
    if (m_executable.isEmpty() || m_installFolder.isEmpty() || m_stateDirectory.isEmpty() || !m_key) return false;
    const QFileInfo file(m_executable);
    return file.isFile() && normalized(file.absolutePath()) == normalized(m_installFolder);
  }

  QString MsiUpdater::markerPath() const {
    return QDir(m_stateDirectory).filePath(QStringLiteral("update-pending.json"));
  }

  QString MsiUpdater::msiPath(const QString& version) const {
    return QDir(m_stateDirectory).filePath(assetName(version));
  }

  void MsiUpdater::prepare(const Release& release) {
    if (!supported() || !m_preparing.isEmpty() || m_readyVersion == release.version) return;
    const QString name = assetName(release.version);
    const ReleaseAsset* file = SignedDownload::findAsset(release, name);
    const ReleaseAsset* signature = SignedDownload::findAsset(release, name + QStringLiteral(".minisig"));
    if (file == nullptr || signature == nullptr) {
      Q_EMIT failed(QStringLiteral("release %1 has no signed MSI").arg(release.version));
      return;
    }
    if (!QDir().mkpath(m_stateDirectory)) {
      Q_EMIT failed(QStringLiteral("cannot write %1").arg(m_stateDirectory));
      return;
    }
    m_preparing = release.version;
    // Downloaded under its final name: the signature check is what makes
    // the file trustworthy, and a failed one removes it.
    m_download.start(*file, *signature, msiPath(release.version), name, *m_key, [this](const QString& error) {
      if (!error.isEmpty()) {
        m_preparing.clear();
        Q_EMIT failed(error);
        return;
      }
      m_readyVersion = m_preparing;
      m_preparing.clear();
      Q_EMIT ready(m_readyVersion);
    });
  }

  bool MsiUpdater::stage(const QString& route) {
    if (!QDir().mkpath(m_stateDirectory)) return false;
    QFile marker(markerPath());
    const QJsonObject state{{QStringLiteral("version"), m_readyVersion},
                            {QStringLiteral("previous"), QCoreApplication::applicationVersion()},
                            {QStringLiteral("route"), route}};
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        marker.write(QJsonDocument(state).toJson()) < 0 || !marker.flush())
      return false;
    marker.close();
    QFile::remove(markerPath() + QStringLiteral(".failed"));

    QFile script(scriptFile(m_stateDirectory));
    const QByteArray text = updateScript().toUtf8();
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate) || script.write(text) != text.size() ||
        !script.flush()) {
      QFile::remove(markerPath());
      return false;
    }
    return true;
  }

  bool MsiUpdater::apply(const QString& route) {
    if (m_readyVersion.isEmpty() || !QFile::exists(msiPath(m_readyVersion))) return false;
#ifdef Q_OS_WIN
    if (!stage(route)) return false;
    // Windows PowerShell 5.1 ships with every supported Windows. The window
    // is hidden; the installer shows its own basic progress (/qb).
    const QStringList arguments =
        scriptArguments(scriptFile(m_stateDirectory), QCoreApplication::applicationPid(), msiPath(m_readyVersion),
                        QDir::toNativeSeparators(m_executable),
                        QDir(m_stateDirectory).filePath(QStringLiteral("update-install.log")), markerPath());
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), arguments, QDir::homePath())) {
      QFile::remove(markerPath());
      return false;
    }
    return true;
#else
    Q_UNUSED(route)
    return false;
#endif
  }

  MsiUpdater::StartState MsiUpdater::confirmStart(const QString& currentVersion) {
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
      result = {StartState::Kind::Updated, version, state.value(QStringLiteral("route")).toString()};
    }
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".failed"));
    return result;
  }

  void MsiUpdater::handleUrl(const QUrl& url) {
    if (url.scheme() == urlScheme()) Q_EMIT restartRequested();
  }

} // namespace jfd::integrations
