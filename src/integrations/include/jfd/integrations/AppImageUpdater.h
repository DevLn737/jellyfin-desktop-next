#pragma once

#include <jfd/integrations/ReleaseSignature.h>
#include <jfd/integrations/SignedDownload.h>
#include <jfd/integrations/UpdateChecker.h>

#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;

// Installs a newer AppImage in place of the running one, and puts the old one
// back when the new one does not start.
//
// prepare() downloads the release's AppImage and its .minisig next to the
// running file and checks the signature. apply() keeps the running file as
// <file>.old, moves the new one in its place and starts a watchdog (sh) that
// waits for this process to exit, starts the new version and gives it 60 s to
// confirm with confirmStart(). Without the confirmation (it crashed, hung or
// exited first) the watchdog moves the old file back and starts it, and that
// start reports the failed version.
namespace jfd::integrations {

  class AppImageUpdater final : public QObject {
    Q_OBJECT
  public:
    // The release file for a version: jellyfin-desktop-next-<version>-x86_64.AppImage.
    static QString assetName(const QString& version);
    // Restart links in the toast use this scheme (QDesktopServices handler).
    static QString urlScheme() { return QStringLiteral("jellyfin-desktop-update"); }

    // appImage: the running file ($APPIMAGE); empty when not run as an
    // AppImage. stateDirectory: where the pending-update marker lives.
    AppImageUpdater(QNetworkAccessManager* network, QString appImage, QString stateDirectory,
                    std::optional<MinisignPublicKey> key, QObject* parent = nullptr);

    // An AppImage with a release key built in, in a folder it can write to.
    bool supported() const;

    void prepare(const Release& release);
    // The version prepare() downloaded and checked, or empty.
    const QString& readyVersion() const { return m_readyVersion; }

    // Swaps the files and starts the watchdog; the caller quits the
    // application right after a true result. route: the screen to reopen.
    bool apply(const QString& route);

    struct StartState {
      enum class Kind { None, Updated, RolledBack };
      Kind kind = Kind::None;
      QString version; // Updated: this one; RolledBack: the one that failed.
      QString route;   // Updated: the screen the old version was on.
    };
    // At start, once the window works: confirms an update that started
    // this version, or reports one that was rolled back.
    StartState confirmStart(const QString& currentVersion);

    // The watchdog's script; public for the tests.
    static QString watchdogScript();

  Q_SIGNALS:
    void ready(const QString& version);
    void failed(const QString& reason);
    // A restart link (urlScheme()) was opened.
    void restartRequested();

  public Q_SLOTS:
    void handleUrl(const QUrl& url);

  private:
    QString markerPath() const;

    QNetworkAccessManager* m_network;
    QString m_appImage;
    QString m_stateDirectory;
    std::optional<MinisignPublicKey> m_key;
    QString m_readyVersion;
    QString m_preparing;
    SignedDownload m_download;
  };

} // namespace jfd::integrations
