#pragma once

#include <jfd/integrations/ReleaseSignature.h>
#include <jfd/integrations/SignedDownload.h>
#include <jfd/integrations/UpdateChecker.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;

// Installs a newer release of the Windows build over the running one.
//
// prepare() downloads the release's MSI and its .minisig into the state
// directory and checks the signature. apply() writes a PowerShell script next
// to it and starts that detached; the script waits for this process to exit,
// runs msiexec on the MSI (the installer is per user, so no administrator
// rights) and starts the application again. The MSI is a major upgrade with a
// fixed UpgradeCode (packaging/windows), so Windows Installer removes the old
// version and installs the new one, and puts the old one back when the
// install fails. The script then starts the old version, which reports the
// failed one through confirmStart(). Nothing here needs the new version to
// confirm in time: the installer either finished or rolled back.
//
// Only the process start in apply() is Windows only; the rest (asset name,
// script, marker) is plain Qt and is tested everywhere.
namespace jfd::integrations {

  class MsiUpdater final : public QObject {
    Q_OBJECT
  public:
    // The release file for a version: jellyfin-desktop-next-<version>-x64.msi.
    static QString assetName(const QString& version);
    // Restart links in the toast use this scheme (QDesktopServices handler).
    static QString urlScheme() { return QStringLiteral("jellyfin-desktop-update"); }
    // The folder the MSI installed to ("InstallFolder" under
    // HKCU\Software\Jellyfin Desktop Next), empty when it is not installed
    // (and everywhere but on Windows).
    static QString installedFolder();

    // executable: the running program; installFolder: where the MSI put the
    // application (installedFolder()). An executable that is not in that
    // folder, such as a build run from its build directory, is not updated.
    // stateDirectory: where the download, script, log and marker live.
    MsiUpdater(QNetworkAccessManager* network, QString executable, QString installFolder, QString stateDirectory,
               std::optional<MinisignPublicKey> key, QObject* parent = nullptr);

    // Installed by the MSI, with a release key built in, and a state directory.
    bool supported() const;

    void prepare(const Release& release);
    // The version prepare() downloaded and checked, or empty.
    const QString& readyVersion() const { return m_readyVersion; }

    // Starts the update script; the caller quits the application right after
    // a true result. route: the screen to reopen. False off Windows.
    bool apply(const QString& route);

    struct StartState {
      enum class Kind { None, Updated, RolledBack };
      Kind kind = Kind::None;
      QString version; // Updated: this one; RolledBack: the one that failed.
      QString route;   // Updated: the screen the old version was on.
    };
    // At start, once the window works: reports an update that started this
    // version, or one the installer could not do.
    StartState confirmStart(const QString& currentVersion);

    // The script apply() writes, and the arguments powershell gets for it;
    // public for the tests.
    static QString updateScript();
    static QStringList scriptArguments(const QString& scriptPath, qint64 pid, const QString& msi,
                                       const QString& executable, const QString& log, const QString& marker);

  Q_SIGNALS:
    void ready(const QString& version);
    void failed(const QString& reason);
    // A restart link (urlScheme()) was opened.
    void restartRequested();

  public Q_SLOTS:
    void handleUrl(const QUrl& url);

  private:
    QString markerPath() const;
    QString msiPath(const QString& version) const;
    // Writes the marker and the script; false when either can't be written.
    bool stage(const QString& route);

    QString m_executable;
    QString m_installFolder;
    QString m_stateDirectory;
    std::optional<MinisignPublicKey> m_key;
    QString m_readyVersion;
    QString m_preparing;
    SignedDownload m_download;
  };

} // namespace jfd::integrations
