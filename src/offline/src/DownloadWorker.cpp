#include <jfd/offline/DownloadWorker.h>

#include <jfd/api/Downloads.h>

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace jfd::offline {

  namespace {
    Q_LOGGING_CATEGORY(lcDownload, "jfd.offline.download")

    constexpr int MaxRedirects = 5;
    constexpr qint64 ProgressIntervalMs = 250;

    struct ContentRange {
      std::int64_t first = -1;
      std::int64_t total = 0;
    };

    // "bytes 100-199/200", "bytes */200"
    ContentRange parseContentRange(const QByteArray& value) {
      static const QRegularExpression pattern(
          QStringLiteral("^bytes\\s+(?:(\\d+)-\\d+|\\*)/(\\d+|\\*)$"),
          QRegularExpression::CaseInsensitiveOption);
      ContentRange range;
      const auto match = pattern.match(QString::fromLatin1(value.trimmed()));
      if (!match.hasMatch()) return range;
      if (match.capturedLength(1) > 0) range.first = match.captured(1).toLongLong();
      range.total = match.captured(2).toLongLong();
      return range;
    }
  } // namespace

  struct DownloadWorker::Transfer {
    DownloadJob job;
    QNetworkReply* reply = nullptr;
    QFile file;
    // Bytes in the file before this request.
    std::int64_t offset = 0;
    // Bytes this attempt wrote.
    std::int64_t received = 0;
    std::int64_t total = 0;
    std::int64_t nextCheck = 0;
    int redirects = 0;
    bool headersSeen = false;
    bool writing = false;
    std::optional<TransferOutcome> forced;
    QElapsedTimer sinceProgress;
  };

  DownloadWorker::DownloadWorker(std::shared_ptr<const StorageGuard> guard, QObject* parent)
      : QObject(parent), m_guard(std::move(guard)) {}

  DownloadWorker::~DownloadWorker() {
    for (auto& [key, transfer] : m_transfers) {
      if (transfer->reply) {
        transfer->reply->disconnect(this);
        transfer->reply->abort();
        transfer->reply->deleteLater();
      }
    }
  }

  void DownloadWorker::start(const DownloadJob& job) {
    if (m_transfers.count(job.key)) return;
    if (!api::isServerUrl(job.serverUrl, job.url)) {
      qCWarning(lcDownload) << "refusing a download that is not on the signed-in server";
      Q_EMIT transferFinished(job.key, static_cast<int>(TransferOutcome::NotServerUrl), 0);
      return;
    }
    if (!m_network) {
      // Created here, on the worker's thread, which owns it from now on.
      m_network = new QNetworkAccessManager(this);
      m_network->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    }
    auto transfer = std::make_unique<Transfer>();
    transfer->job = job;
    transfer->file.setFileName(job.partPath);
    Transfer& t = *transfer;
    m_transfers.emplace(job.key, std::move(transfer));

    if (!QDir().mkpath(QFileInfo(job.partPath).path())) {
      finish(job.key, TransferOutcome::DiskError);
      return;
    }
    const QFileInfo part(job.partPath);
    t.offset = part.exists() ? part.size() : 0;
    if (job.resumeValidator.isEmpty() || (job.expectedSize > 0 && t.offset > job.expectedSize))
      t.offset = 0;
    request(t);
  }

  void DownloadWorker::request(Transfer& t) {
    QNetworkRequest request(t.job.url);
    request.setRawHeader("Authorization", t.job.authorization);
    // The bytes as stored: no transparent decompression, which would break
    // byte ranges.
    request.setRawHeader("Accept-Encoding", "identity");
    if (t.offset > 0) {
      request.setRawHeader("Range", "bytes=" + QByteArray::number(t.offset) + '-');
      request.setRawHeader("If-Range", t.job.resumeValidator.toLatin1());
    }
    if (m_stallTimeoutMs > 0) request.setTransferTimeout(m_stallTimeoutMs);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);

    QNetworkReply* reply = m_network->get(request);
    t.reply = reply;
    t.headersSeen = false;
    const QString key = t.job.key;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, key]() {
      const auto it = m_transfers.find(key);
      if (it != m_transfers.end()) onHeaders(*it->second);
    });
    connect(reply, &QIODevice::readyRead, this, [this, key]() {
      auto it = m_transfers.find(key);
      if (it == m_transfers.end()) return;
      if (!it->second->headersSeen) {
        // onHeaders may abort, which finishes and forgets the transfer.
        onHeaders(*it->second);
        it = m_transfers.find(key);
        if (it == m_transfers.end()) return;
      }
      onData(*it->second);
    });
    connect(reply, &QNetworkReply::finished, this, [this, key]() { onFinished(key); });
  }

  void DownloadWorker::onHeaders(Transfer& t) {
    if (t.headersSeen || t.forced) return;
    const int status = t.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 206) return;
    t.headersSeen = true;

    if (status == 206) {
      const ContentRange range = parseContentRange(t.reply->rawHeader("Content-Range"));
      if (range.first != t.offset) {
        // Not the range asked for: drop the old bytes and start over.
        qCWarning(lcDownload) << "unexpected Content-Range; starting the file over";
        t.forced = TransferOutcome::SizeMismatch;
        t.reply->abort();
        return;
      }
      t.total = range.total;
      if (!t.file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        t.forced = TransferOutcome::DiskError;
        t.reply->abort();
        return;
      }
    } else {
      // A whole file: either the first request, a server without ranges, or
      // a file that changed since the .part was written (If-Range failed).
      t.offset = 0;
      const qint64 length = t.reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
      t.total = length > 0 ? length : 0;
      if (!t.file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        t.forced = TransferOutcome::DiskError;
        t.reply->abort();
        return;
      }
    }
    t.writing = true;
    t.nextCheck = m_checkEvery;
    t.sinceProgress.start();

    QByteArray validator = t.reply->rawHeader("ETag");
    // A weak ETag cannot validate a byte range (RFC 9110, 13.1.5).
    if (validator.startsWith("W/")) validator.clear();
    if (validator.isEmpty()) validator = t.reply->rawHeader("Last-Modified");
    Q_EMIT transferStarted(t.job.key, t.offset, t.total, QString::fromLatin1(validator));
    if (m_guard && m_guard->check(QFileInfo(t.job.partPath).path()) != StorageVerdict::Ok) {
      t.forced = TransferOutcome::LowSpace;
      t.reply->abort();
    }
  }

  void DownloadWorker::onData(Transfer& t) {
    if (!t.writing || t.forced) {
      t.reply->readAll();
      return;
    }
    const QByteArray data = t.reply->readAll();
    if (data.isEmpty()) return;
    if (t.file.write(data) != data.size()) {
      t.forced = TransferOutcome::DiskError;
      t.reply->abort();
      return;
    }
    t.received += data.size();
    if (t.received >= t.nextCheck) {
      t.nextCheck = t.received + m_checkEvery;
      if (m_guard && m_guard->check(QFileInfo(t.job.partPath).path()) != StorageVerdict::Ok) {
        t.forced = TransferOutcome::LowSpace;
        t.reply->abort();
        return;
      }
    }
    emitProgress(t, false);
  }

  void DownloadWorker::emitProgress(Transfer& t, bool force) {
    if (!force && t.sinceProgress.isValid() && t.sinceProgress.elapsed() < ProgressIntervalMs)
      return;
    t.sinceProgress.restart();
    Q_EMIT transferProgress(t.job.key, t.offset + t.received, t.total);
  }

  void DownloadWorker::onFinished(const QString& key) {
    const auto it = m_transfers.find(key);
    if (it == m_transfers.end()) return;
    Transfer& t = *it->second;
    QNetworkReply* reply = t.reply;
    t.reply = nullptr;
    reply->deleteLater();

    if (!t.forced && t.writing && reply->bytesAvailable() > 0) {
      const QByteArray rest = reply->readAll();
      if (t.file.write(rest) != rest.size())
        t.forced = TransferOutcome::DiskError;
      else
        t.received += rest.size();
    }
    if (t.file.isOpen()) {
      if (!t.file.flush() && !t.forced) t.forced = TransferOutcome::DiskError;
      t.file.close();
    }
    if (t.forced) {
      finish(key, *t.forced);
      return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400 && reply->hasRawHeader("Location")) {
      const QUrl target = t.job.url.resolved(QUrl::fromEncoded(reply->rawHeader("Location")));
      if (!api::isServerUrl(t.job.serverUrl, target) || ++t.redirects > MaxRedirects) {
        qCWarning(lcDownload) << "not following a redirect off the server";
        finish(key, TransferOutcome::NotServerUrl);
        return;
      }
      t.job.url = target;
      request(t);
      return;
    }
    if (status == 416) {
      // The range starts at or past the end: the .part may be complete.
      const ContentRange range = parseContentRange(reply->rawHeader("Content-Range"));
      const qint64 onDisk = QFileInfo(t.job.partPath).size();
      if (range.total > 0 && onDisk == range.total &&
          (t.job.expectedSize == 0 || t.job.expectedSize == range.total)) {
        t.total = range.total;
      } else {
        QFile::remove(t.job.partPath);
        finish(key, TransferOutcome::SizeMismatch);
        return;
      }
    } else if (status == 401) {
      finish(key, TransferOutcome::AuthRequired);
      return;
    } else if (status >= 400 && status < 500) {
      finish(key, TransferOutcome::Refused);
      return;
    } else if (reply->error() != QNetworkReply::NoError || (status != 200 && status != 206)) {
      finish(key, TransferOutcome::NetworkError);
      return;
    }

    const qint64 onDisk = QFileInfo(t.job.partPath).size();
    const std::int64_t want = t.total > 0 ? t.total : t.job.expectedSize;
    if (want > 0 && onDisk < want) {
      // The body ended early without an error (the connection closed
      // before Content-Length was reached): resume like any dropped
      // connection, from what is on disk.
      qCInfo(lcDownload) << "body ended at" << onDisk << "of" << want << "bytes";
      finish(key, TransferOutcome::NetworkError);
      return;
    }
    if (want > 0 && onDisk > want) {
      qCWarning(lcDownload) << "size on disk" << onDisk << "is more than" << want;
      finish(key, TransferOutcome::SizeMismatch);
      return;
    }
    if (QFileInfo::exists(t.job.finalPath)) QFile::remove(t.job.finalPath);
    if (!QFile::rename(t.job.partPath, t.job.finalPath)) {
      finish(key, TransferOutcome::DiskError);
      return;
    }
    emitProgress(t, true);
    finish(key, TransferOutcome::Complete);
  }

  void DownloadWorker::finish(const QString& key, TransferOutcome outcome) {
    const auto it = m_transfers.find(key);
    if (it == m_transfers.end()) return;
    const std::int64_t received = it->second->received;
    if (it->second->reply) {
      it->second->reply->disconnect(this);
      it->second->reply->abort();
      it->second->reply->deleteLater();
    }
    m_transfers.erase(it);
    Q_EMIT transferFinished(key, static_cast<int>(outcome), received);
  }

  void DownloadWorker::stop(const QString& key) {
    const auto it = m_transfers.find(key);
    if (it == m_transfers.end()) return;
    Transfer& t = *it->second;
    t.forced = TransferOutcome::Cancelled;
    if (t.reply)
      t.reply->abort();
    else
      finish(key, TransferOutcome::Cancelled);
  }

  void DownloadWorker::stopAll() {
    QStringList keys;
    for (const auto& [key, transfer] : m_transfers)
      keys.append(key);
    for (const QString& key : keys)
      stop(key);
  }

} // namespace jfd::offline
