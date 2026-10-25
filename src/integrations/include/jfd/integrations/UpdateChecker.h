#pragma once

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;

// Compares the running version with the newest release of the client on
// GitHub and reports a newer one with its download page. It never downloads
// or installs anything.
namespace jfd::integrations {

  // Semantic versions: "1.2.3", "v1.2.3", "1.2.3-rc.1". A pre-release sorts
  // before its release. Unparsable versions compare as not newer.
  bool isNewerVersion(const QString& candidate, const QString& current);

  // Stable: releases only. Beta: pre-releases too (the first builds for the
  // group are pre-releases).
  enum class UpdateChannel { Stable, Beta };
  // "stable" or "beta" (the setting's values); anything else is Beta.
  UpdateChannel updateChannel(const QString& value);

  struct ReleaseAsset {
    QString name;
    QUrl url; // browser_download_url
    qint64 size = 0;
  };

  struct Release {
    QString version; // Without the leading "v".
    QUrl page;       // The release page on github.com.
    bool prerelease = false;
    QList<ReleaseAsset> assets;
  };

  // The newest client release in a GitHub releases list that the channel
  // allows. Only tags "v<semver>" are the client's: other releases of the
  // repository (test payloads and the like) are never offered. Drafts and
  // releases whose page is not on github.com are skipped.
  std::optional<Release> newestRelease(const QJsonArray& releases, UpdateChannel channel);

  class UpdateChecker final : public QObject {
    Q_OBJECT
  public:
    static QUrl defaultReleasesUrl();

    // releasesUrl: the GitHub API's releases list endpoint.
    UpdateChecker(QNetworkAccessManager* network, QString currentVersion, UpdateChannel channel,
                  QUrl releasesUrl = defaultReleasesUrl(), QObject* parent = nullptr);

    // One request; the result arrives as one of the signals.
    void check();

    // The release last reported by updateAvailable.
    const std::optional<Release>& available() const { return m_available; }

  Q_SIGNALS:
    // downloadPage: the release page on GitHub, where the files are.
    void updateAvailable(const QString& version, const QUrl& downloadPage);
    void upToDate();
    // Network errors, no client release, unexpected answers. Not shown to
    // the user; the next start tries again.
    void checkFailed(const QString& reason);

  private:
    QNetworkAccessManager* m_network;
    QString m_currentVersion;
    UpdateChannel m_channel;
    QUrl m_url;
    std::optional<Release> m_available;
  };

} // namespace jfd::integrations
