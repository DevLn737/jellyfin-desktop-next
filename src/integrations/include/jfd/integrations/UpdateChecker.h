#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

// Compares the running version with the latest published release on GitHub
// and reports a newer one with its download page. It never downloads or
// installs anything. Pre-releases and drafts are not offered (GitHub's
// "latest release" excludes them).
namespace jfd::integrations {

  // Semantic versions: "1.2.3", "v1.2.3", "1.2.3-rc.1". A pre-release sorts
  // before its release. Unparsable versions compare as not newer.
  bool isNewerVersion(const QString& candidate, const QString& current);

  class UpdateChecker final : public QObject {
    Q_OBJECT
  public:
    static QUrl defaultReleasesUrl();

    // latestReleaseUrl: the GitHub API's releases/latest endpoint.
    UpdateChecker(QNetworkAccessManager* network, QString currentVersion, QUrl latestReleaseUrl = defaultReleasesUrl(),
                  QObject* parent = nullptr);

    // One request; the result arrives as one of the signals.
    void check();

  Q_SIGNALS:
    // downloadPage: the release page on GitHub, where the files are.
    void updateAvailable(const QString& version, const QUrl& downloadPage);
    void upToDate();
    // Network errors, no published release, unexpected answers. Not shown to
    // the user; the next start tries again.
    void checkFailed(const QString& reason);

  private:
    QNetworkAccessManager* m_network;
    QString m_currentVersion;
    QUrl m_url;
  };

} // namespace jfd::integrations
