#include <jfd/integrations/UpdateChecker.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <array>
#include <optional>

namespace jfd::integrations {

  namespace {
    struct Version {
      std::array<qulonglong, 3> numbers{};
      QStringList prerelease;
    };

    std::optional<Version> parse(const QString& text) {
      static const QRegularExpression pattern(
          QStringLiteral(R"(^v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$)"));
      const QRegularExpressionMatch match = pattern.match(text.trimmed());
      if (!match.hasMatch()) return std::nullopt;
      Version version;
      for (int i = 0; i < 3; ++i) {
        bool ok = false;
        version.numbers[static_cast<size_t>(i)] = match.captured(i + 1).toULongLong(&ok);
        if (!ok) return std::nullopt;
      }
      if (match.hasCaptured(4)) version.prerelease = match.captured(4).split(QLatin1Char('.'));
      return version;
    }

    // SemVer 2.0 precedence of pre-release identifiers.
    int comparePrerelease(const QStringList& a, const QStringList& b) {
      if (a.isEmpty() || b.isEmpty()) return a.isEmpty() == b.isEmpty() ? 0 : (a.isEmpty() ? 1 : -1);
      for (qsizetype i = 0; i < std::min(a.size(), b.size()); ++i) {
        bool aNumeric = false, bNumeric = false;
        const qulonglong an = a[i].toULongLong(&aNumeric);
        const qulonglong bn = b[i].toULongLong(&bNumeric);
        if (aNumeric && bNumeric) {
          if (an != bn) return an < bn ? -1 : 1;
        } else if (aNumeric != bNumeric) {
          return aNumeric ? -1 : 1;
        } else if (const int c = QString::compare(a[i], b[i]); c != 0) {
          return c < 0 ? -1 : 1;
        }
      }
      return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
    }
  } // namespace

  bool isNewerVersion(const QString& candidate, const QString& current) {
    const auto a = parse(candidate);
    const auto b = parse(current);
    if (!a || !b) return false;
    if (a->numbers != b->numbers) return a->numbers > b->numbers;
    return comparePrerelease(a->prerelease, b->prerelease) > 0;
  }

  QUrl UpdateChecker::defaultReleasesUrl() {
    return QUrl(QStringLiteral("https://api.github.com/repos/DevLn737/jellyfin-desktop-next/releases/latest"));
  }

  UpdateChecker::UpdateChecker(QNetworkAccessManager* network, QString currentVersion, QUrl latestReleaseUrl,
                               QObject* parent)
      : QObject(parent), m_network(network), m_currentVersion(std::move(currentVersion)),
        m_url(std::move(latestReleaseUrl)) {}

  void UpdateChecker::check() {
    QNetworkRequest request(m_url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("jellyfin-desktop-next/%1").arg(m_currentVersion));
    request.setTransferTimeout(15000);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
      reply->deleteLater();
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (reply->error() != QNetworkReply::NoError && status == 0) {
        Q_EMIT checkFailed(reply->errorString());
        return;
      }
      if (status != 200) {
        // 404: nothing published yet.
        Q_EMIT checkFailed(QStringLiteral("HTTP %1").arg(status));
        return;
      }
      const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
      const QString tag = release.value(QStringLiteral("tag_name")).toString();
      const QUrl page(release.value(QStringLiteral("html_url")).toString());
      if (tag.isEmpty() || release.value(QStringLiteral("draft")).toBool() ||
          release.value(QStringLiteral("prerelease")).toBool()) {
        Q_EMIT checkFailed(QStringLiteral("no published release"));
        return;
      }
      // Only GitHub pages are offered as download links.
      if (page.scheme() != QStringLiteral("https") || page.host() != QStringLiteral("github.com")) {
        Q_EMIT checkFailed(QStringLiteral("unexpected release page"));
        return;
      }
      if (isNewerVersion(tag, m_currentVersion))
        Q_EMIT updateAvailable(tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag, page);
      else
        Q_EMIT upToDate();
    });
  }

} // namespace jfd::integrations
