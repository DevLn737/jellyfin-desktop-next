#pragma once

#include <jfd/offline/StorageGuard.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <map>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

// The part of the downloads that touches the network and the disk. It lives
// on a worker thread with its own QNetworkAccessManager, so writing 50-100
// MB/s never stalls the interface; the queue (GUI thread) talks to it by
// queued calls and signals.
//
// Resuming: when `media.part` has bytes and the job carries the validator
// (ETag or Last-Modified) of the response they came from, the request asks
// for the rest with "Range: bytes=<size>-" and "If-Range: <validator>". A
// 206 is appended; a 200 (no range support, or the file changed) starts the
// file over. The finished file is checked against the expected size before
// it is renamed from .part.
//
// The token: the Authorization header goes only to the server's own scheme,
// host and port. A redirect elsewhere is not followed.
namespace jfd::offline {

  enum class TransferOutcome {
    Complete,
    // stop() was called.
    Cancelled,
    // No answer, a dropped connection or a 5xx: worth retrying.
    NetworkError,
    // The size on disk does not match the server's: retry from scratch.
    SizeMismatch,
    // 401: the session is gone.
    AuthRequired,
    // Another 4xx (403 without download permission, 404): no retry.
    Refused,
    // The URL, or a redirect, leads off the server.
    NotServerUrl,
    // The disk reached the reserve while writing.
    LowSpace,
    // Writing or renaming failed.
    DiskError,
  };

  struct DownloadJob {
    // The queue's name for the job.
    QString key;
    QUrl url;
    // The signed-in server; the token goes nowhere else.
    QUrl serverUrl;
    QByteArray authorization;
    QString partPath;
    QString finalPath;
    // 0 when unknown.
    std::int64_t expectedSize = 0;
    // If-Range for the bytes already in partPath; empty: start over.
    QString resumeValidator;
  };

  class DownloadWorker : public QObject {
    Q_OBJECT

  public:
    // `guard` is asked every `checkEveryBytes` written (64 MiB).
    explicit DownloadWorker(std::shared_ptr<const StorageGuard> guard, QObject* parent = nullptr);
    ~DownloadWorker() override;

    void setCheckEveryBytes(std::int64_t bytes) { m_checkEvery = bytes; }
    // A transfer with no data for this long fails (NetworkError).
    void setStallTimeoutMs(int ms) { m_stallTimeoutMs = ms; }

    // Call on the worker's thread.
    void start(const DownloadJob& job);
    void stop(const QString& key);
    void stopAll();

  Q_SIGNALS:
    // The server answered with data: `offset` bytes are kept from before,
    // `total` is the whole file's size (0 if unknown), `validator` is what to
    // send as If-Range next time (empty if the server gave none).
    void transferStarted(const QString& key, qint64 offset, qint64 total, const QString& validator);
    // At most four times a second per transfer.
    void transferProgress(const QString& key, qint64 bytesOnDisk, qint64 total);
    // `received` is what this attempt wrote.
    void transferFinished(const QString& key, int outcome, qint64 received);

  private:
    struct Transfer;

    void request(Transfer& transfer);
    void onHeaders(Transfer& transfer);
    void onData(Transfer& transfer);
    void onFinished(const QString& key);
    void finish(const QString& key, TransferOutcome outcome);
    void emitProgress(Transfer& transfer, bool force);

    std::shared_ptr<const StorageGuard> m_guard;
    QNetworkAccessManager* m_network = nullptr;
    std::map<QString, std::unique_ptr<Transfer>> m_transfers;
    std::int64_t m_checkEvery = 64 * MiB;
    int m_stallTimeoutMs = 60'000;
  };

} // namespace jfd::offline
