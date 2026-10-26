#include <jfd/integrations/SignedDownload.h>

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace jfd::integrations {

  SignedDownload::SignedDownload(QNetworkAccessManager* network, QObject* parent)
      : QObject(parent), m_network(network) {}

  SignedDownload::~SignedDownload() {
    if (m_reply) m_reply->abort();
    delete m_file;
  }

  const ReleaseAsset* SignedDownload::findAsset(const Release& release, const QString& name) {
    for (const ReleaseAsset& asset : release.assets) {
      if (asset.name == name) return &asset;
    }
    return nullptr;
  }

  void SignedDownload::fail(const QString& reason) {
    QFile::remove(m_path);
    QFile::remove(m_path + QStringLiteral(".minisig"));
    Done done = std::move(m_done);
    if (done) done(reason);
  }

  void SignedDownload::download(const QUrl& url, const QString& path, qint64 size, std::function<void()> done) {
    delete m_file;
    m_file = new QFile(path);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      fail(QStringLiteral("cannot write %1").arg(path));
      return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
      if (m_file->write(reply->readAll()) < 0) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, size, done = std::move(done)] {
      reply->deleteLater();
      m_file->write(reply->readAll());
      const qint64 written = m_file->size();
      const bool flushed = m_file->flush();
      m_file->close();
      if (reply->error() != QNetworkReply::NoError) {
        fail(QStringLiteral("download failed: %1").arg(reply->errorString()));
      } else if (!flushed || (size > 0 && written != size)) {
        fail(QStringLiteral("download incomplete: %1 of %2 bytes").arg(written).arg(size));
      } else {
        done();
      }
    });
  }

  void SignedDownload::start(const ReleaseAsset& file, const ReleaseAsset& signature, const QString& path,
                             const QString& assetName, const MinisignPublicKey& key, Done done) {
    m_path = path;
    m_done = std::move(done);
    const QString signaturePath = path + QStringLiteral(".minisig");
    const QUrl signatureUrl = signature.url;
    const qint64 signatureSize = signature.size;
    download(file.url, path, file.size, [this, path, signaturePath, signatureUrl, signatureSize, assetName, key] {
      download(signatureUrl, signaturePath, signatureSize, [this, path, signaturePath, assetName, key] {
        QFile signatureFile(signaturePath);
        QFile data(path);
        const bool opened = signatureFile.open(QIODevice::ReadOnly) && data.open(QIODevice::ReadOnly);
        const QByteArray signatureText = opened ? signatureFile.read(4096) : QByteArray();
        const SignatureCheck check = opened ? verifyReleaseFile(data, signatureText, assetName, key)
                                            : SignatureCheck{false, QStringLiteral("cannot read the download"), {}};
        data.close();
        signatureFile.close();
        QFile::remove(signaturePath);
        if (!check.valid) {
          fail(QStringLiteral("signature check failed: %1").arg(check.error));
          return;
        }
        Done finished = std::move(m_done);
        if (finished) finished(QString());
      });
    });
  }

} // namespace jfd::integrations
