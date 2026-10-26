#pragma once

#include <jfd/integrations/ReleaseSignature.h>
#include <jfd/integrations/UpdateChecker.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

// The part of an update every platform shares: a release file and its
// .minisig are downloaded and the signature is checked, so what is left on
// disk was built and signed by the project's CI (AppImageUpdater, MsiUpdater).
namespace jfd::integrations {

  class SignedDownload final : public QObject {
    Q_OBJECT
  public:
    // The error text of a failed download, empty when it worked.
    using Done = std::function<void(const QString& error)>;

    explicit SignedDownload(QNetworkAccessManager* network, QObject* parent = nullptr);
    ~SignedDownload() override;

    // The asset of a release by name, or nullptr.
    static const ReleaseAsset* findAsset(const Release& release, const QString& name);

    // Downloads file to path and its signature to path + ".minisig", and
    // checks it against key as the release file assetName. On success the
    // file is at path and the signature is gone; on failure neither is left.
    // One download at a time.
    void start(const ReleaseAsset& file, const ReleaseAsset& signature, const QString& path,
               const QString& assetName, const MinisignPublicKey& key, Done done);

  private:
    void download(const QUrl& url, const QString& path, qint64 size, std::function<void()> done);
    void fail(const QString& reason);

    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    QFile* m_file = nullptr;
    QString m_path;
    Done m_done;
  };

} // namespace jfd::integrations
