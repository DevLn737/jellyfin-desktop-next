#include <jfd/integrations/UpdateChecker.h>

#include <QJsonArray>
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

  UpdateChannel updateChannel(const QString& value) {
    return value == QLatin1String("stable") ? UpdateChannel::Stable : UpdateChannel::Beta;
  }

  std::optional<Release> newestRelease(const QJsonArray& releases, UpdateChannel channel) {
    static const QRegularExpression clientTag(QStringLiteral(R"(^v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$)"));
    std::optional<Release> newest;
    for (const QJsonValue& value : releases) {
      const QJsonObject release = value.toObject();
      const QString tag = release.value(QStringLiteral("tag_name")).toString();
      const bool prerelease = release.value(QStringLiteral("prerelease")).toBool();
      if (!clientTag.match(tag).hasMatch() || release.value(QStringLiteral("draft")).toBool()) continue;
      if (prerelease && channel == UpdateChannel::Stable) continue;
      // Only GitHub pages are offered as download links.
      const QUrl page(release.value(QStringLiteral("html_url")).toString());
      if (page.scheme() != QStringLiteral("https") || page.host() != QStringLiteral("github.com")) continue;
      const QString version = tag.mid(1);
      if (newest && !isNewerVersion(version, newest->version)) continue;

      Release candidate;
      candidate.version = version;
      candidate.page = page;
      candidate.prerelease = prerelease;
      for (const QJsonValue& assetValue : release.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = assetValue.toObject();
        candidate.assets.append({asset.value(QStringLiteral("name")).toString(),
                                 QUrl(asset.value(QStringLiteral("browser_download_url")).toString()),
                                 asset.value(QStringLiteral("size")).toInteger()});
      }
      newest = std::move(candidate);
    }
    return newest;
  }

  QUrl UpdateChecker::defaultReleasesUrl() {
    return QUrl(QStringLiteral("https://api.github.com/repos/DevLn737/jellyfin-desktop-next/releases?per_page=30"));
  }

  UpdateChecker::UpdateChecker(QNetworkAccessManager* network, QString currentVersion, UpdateChannel channel,
                               QUrl releasesUrl, QObject* parent)
      : QObject(parent), m_network(network), m_currentVersion(std::move(currentVersion)), m_channel(channel),
        m_url(std::move(releasesUrl)) {}

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
        Q_EMIT checkFailed(QStringLiteral("HTTP %1").arg(status));
        return;
      }
      const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
      if (!document.isArray()) {
        Q_EMIT checkFailed(QStringLiteral("unexpected answer"));
        return;
      }
      const auto newest = newestRelease(document.array(), m_channel);
      if (!newest) {
        Q_EMIT checkFailed(QStringLiteral("no client release"));
        return;
      }
      if (isNewerVersion(newest->version, m_currentVersion)) {
        m_available = newest;
        Q_EMIT updateAvailable(newest->version, newest->page);
      } else {
        Q_EMIT upToDate();
      }
    });
  }

} // namespace jfd::integrations
