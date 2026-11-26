// Compressed copies: the server's HLS VOD transcode, segment by segment, into
// one fragmented MP4 (init segment, then the media segments in order). See
// DownloadWorker.h and docs/offline.md ("Compressed copies").

#include <jfd/offline/DownloadWorker.h>

#include "DownloadTransfer.h"
#include "FileSync.h"

#include <jfd/api/Downloads.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace jfd::offline {

  namespace {
    Q_LOGGING_CATEGORY(lcTranscode, "jfd.offline.transcode")

    constexpr int MaxRedirects = 5;

    QString queryValue(const QUrl& url, const char* name) {
      const QUrlQuery query(url);
      for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded))
        if (key.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) return value;
      return {};
    }

    QString hashOf(const QByteArray& data) {
      return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    }
  } // namespace

  void DownloadWorker::hlsRequest(Transfer& t, const QUrl& url, int step) {
    t.hlsStep = static_cast<Transfer::HlsStep>(step);
    t.hlsUrl = url;
    if (!api::isServerUrl(t.job.serverUrl, url)) {
      qCWarning(lcTranscode) << "refusing a transcode URL that is not on the signed-in server";
      finish(t.job.key, TransferOutcome::NotServerUrl);
      return;
    }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", t.job.authorization);
    request.setRawHeader("Accept-Encoding", "identity");
    if (m_transcodeTimeoutMs > 0) request.setTransferTimeout(m_transcodeTimeoutMs);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = nullptr;
    if (t.hlsStep == Transfer::HlsStep::PlaybackInfo) {
      request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
      reply = m_network->post(request, QJsonDocument(t.job.transcode->playbackInfoBody)
                                           .toJson(QJsonDocument::Compact));
    } else {
      reply = m_network->get(request);
    }
    t.reply = reply;
    const QString key = t.job.key;
    connect(reply, &QNetworkReply::finished, this, [this, key]() { onHlsReply(key); });
  }

  void DownloadWorker::onHlsReply(const QString& key) {
    const auto it = m_transfers.find(key);
    if (it == m_transfers.end()) return;
    Transfer& t = *it->second;
    QNetworkReply* reply = t.reply;
    t.reply = nullptr;
    reply->deleteLater();
    if (t.forced) {
      finish(key, *t.forced);
      return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400 && reply->hasRawHeader("Location")) {
      const QUrl target = t.hlsUrl.resolved(QUrl::fromEncoded(reply->rawHeader("Location")));
      if (!api::isServerUrl(t.job.serverUrl, target) || ++t.redirects > MaxRedirects) {
        qCWarning(lcTranscode) << "not following a redirect off the server";
        finish(key, TransferOutcome::NotServerUrl);
        return;
      }
      // A redirected PlaybackInfo POST is not repeated as a GET.
      if (t.hlsStep == Transfer::HlsStep::PlaybackInfo) {
        finish(key, TransferOutcome::Refused);
        return;
      }
      hlsRequest(t, target, static_cast<int>(t.hlsStep));
      return;
    }
    if (status == 401) {
      finish(key, TransferOutcome::AuthRequired);
      return;
    }
    if (status >= 400 && status < 500) {
      qCInfo(lcTranscode) << "the server refused the transcode:" << status;
      finish(key, TransferOutcome::Refused);
      return;
    }
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
      finish(key, TransferOutcome::NetworkError);
      return;
    }
    const QByteArray body = reply->readAll();
    switch (t.hlsStep) {
    case Transfer::HlsStep::PlaybackInfo:
      onHlsPlaybackInfo(t, body);
      break;
    case Transfer::HlsStep::Master:
      onHlsMaster(t, body);
      break;
    case Transfer::HlsStep::Media:
      onHlsMedia(t, body);
      break;
    case Transfer::HlsStep::Init:
      onHlsInit(t, body);
      break;
    case Transfer::HlsStep::Segment:
      onHlsSegment(t, body);
      break;
    }
  }

  void DownloadWorker::onHlsPlaybackInfo(Transfer& t, const QByteArray& body) {
    const QJsonObject info = QJsonDocument::fromJson(body).object();
    if (!info.value(QLatin1String("ErrorCode")).toString().isEmpty()) {
      qCInfo(lcTranscode) << "PlaybackInfo:" << info.value(QLatin1String("ErrorCode")).toString();
      finish(t.job.key, TransferOutcome::Refused);
      return;
    }
    const QString sourceId = t.job.transcode->playbackInfoBody.value(QLatin1String("MediaSourceId")).toString();
    QJsonObject source;
    for (const QJsonValue& value : info.value(QLatin1String("MediaSources")).toArray()) {
      const QJsonObject candidate = value.toObject();
      if (candidate.value(QLatin1String("TranscodingUrl")).toString().isEmpty()) continue;
      if (source.isEmpty() || candidate.value(QLatin1String("Id")).toString() == sourceId)
        source = candidate;
    }
    // No TranscodingUrl: the user may not transcode, or the server cannot.
    if (source.isEmpty() ||
        source.value(QLatin1String("TranscodingSubProtocol")).toString() != QLatin1String("hls")) {
      qCInfo(lcTranscode) << "the server offers no HLS transcode for this item";
      finish(t.job.key, TransferOutcome::Refused);
      return;
    }
    const QUrl master = api::transcodingUrlOnServer(
        t.job.serverUrl, source.value(QLatin1String("TranscodingUrl")).toString());
    if (master.isEmpty()) {
      finish(t.job.key, TransferOutcome::NotServerUrl);
      return;
    }
    t.deviceId = queryValue(master, "DeviceId");
    t.playSessionId = info.value(QLatin1String("PlaySessionId")).toString();
    if (t.playSessionId.isEmpty()) t.playSessionId = queryValue(master, "PlaySessionId");
    const QString audio = queryValue(master, "AudioStreamIndex");
    bool audioOk = false;
    const int audioIndex = audio.toInt(&audioOk);
    t.transcodeState = QJsonObject{
        {QStringLiteral("audioStreamIndex"), audioOk ? audioIndex : -1},
        {QStringLiteral("videoCodec"), queryValue(master, "VideoCodec").section(QLatin1Char(','), 0, 0)},
        {QStringLiteral("audioCodec"), queryValue(master, "AudioCodec").section(QLatin1Char(','), 0, 0)},
    };
    hlsRequest(t, master, static_cast<int>(Transfer::HlsStep::Master));
  }

  void DownloadWorker::onHlsMaster(Transfer& t, const QByteArray& body) {
    const QString variant = hlsFirstVariant(body);
    if (variant.isEmpty()) {
      qCWarning(lcTranscode) << "the master playlist names no media playlist";
      finish(t.job.key, TransferOutcome::NetworkError);
      return;
    }
    t.mediaPlaylistUrl = t.hlsUrl.resolved(QUrl(variant));
    hlsRequest(t, t.mediaPlaylistUrl, static_cast<int>(Transfer::HlsStep::Media));
  }

  void DownloadWorker::onHlsMedia(Transfer& t, const QByteArray& body) {
    const auto playlist = parseHlsMediaPlaylist(body);
    if (!playlist) {
      qCWarning(lcTranscode) << "not a complete VOD playlist";
      finish(t.job.key, TransferOutcome::Refused);
      return;
    }
    t.playlist = *playlist;
    const TranscodeState& resume = t.job.transcode->resume;
    const QFileInfo part(t.job.partPath);
    t.resumeCandidate = resume.segmentsDone > 0 && resume.partBytes > 0 &&
                        resume.segmentCount == playlist->segments.size() &&
                        resume.signature == playlist->signature() && part.exists() &&
                        part.size() >= resume.partBytes;
    if (resume.segmentsDone > 0 && !t.resumeCandidate)
      qCInfo(lcTranscode) << "the transcode differs from the one in .part; starting over";
    t.transcodeState.insert(QStringLiteral("segmentCount"), static_cast<int>(playlist->segments.size()));
    t.transcodeState.insert(QStringLiteral("signature"), playlist->signature());
    if (playlist->mapUri.isEmpty()) {
      onHlsInit(t, {});
      return;
    }
    hlsRequest(t, t.mediaPlaylistUrl.resolved(QUrl(playlist->mapUri)), static_cast<int>(Transfer::HlsStep::Init));
  }

  void DownloadWorker::onHlsInit(Transfer& t, const QByteArray& body) {
    const TranscodeState& resume = t.job.transcode->resume;
    const QString initHash = hashOf(body);
    t.transcodeState.insert(QStringLiteral("initHash"), initHash);
    const std::int64_t estimate = t.job.transcode->estimatedSize;
    if (t.resumeCandidate && resume.initHash == initHash) {
      if (!t.file.resize(resume.partBytes)) {
        finish(t.job.key, TransferOutcome::DiskError);
        return;
      }
      t.partBytes = resume.partBytes;
      t.nextSegment = resume.segmentsDone;
      qCInfo(lcTranscode) << "resuming at segment" << t.nextSegment << "of" << t.playlist.segments.size();
    } else {
      if (t.resumeCandidate)
        qCInfo(lcTranscode) << "the init segment differs from the one in .part; starting over";
      if (!t.file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
          t.file.write(body) != body.size() || !t.file.flush()) {
        t.file.close();
        finish(t.job.key, writeFailure(m_guard.get(), t.job.partPath));
        return;
      }
      t.file.close();
      t.partBytes = body.size();
      t.nextSegment = 0;
    }
    t.offset = t.partBytes;
    t.total = estimate;
    t.nextCheck = m_checkEvery;
    t.sinceProgress.start();
    t.transcodeState.insert(QStringLiteral("estimatedSize"), static_cast<double>(estimate));
    Q_EMIT transcodeStarted(t.job.key, t.transcodeState);
    Q_EMIT segmentWritten(t.job.key, t.nextSegment, t.partBytes);
    if (m_guard && m_guard->check(QFileInfo(t.job.partPath).path()) != StorageVerdict::Ok) {
      finish(t.job.key, TransferOutcome::LowSpace);
      return;
    }
    hlsNextSegment(t);
  }

  void DownloadWorker::hlsNextSegment(Transfer& t) {
    if (t.nextSegment < t.playlist.segments.size()) {
      hlsRequest(t, t.mediaPlaylistUrl.resolved(QUrl(t.playlist.segments.at(t.nextSegment).uri)),
                 static_cast<int>(Transfer::HlsStep::Segment));
      return;
    }
    const qint64 onDisk = QFileInfo(t.job.partPath).size();
    if (onDisk != t.partBytes) {
      qCWarning(lcTranscode) << "size on disk" << onDisk << "is not" << t.partBytes;
      finish(t.job.key, TransferOutcome::SizeMismatch);
      return;
    }
    // On the disk before it takes the final name (see DownloadWorker.cpp).
    if (!syncFileToDisk(t.job.partPath)) qCWarning(lcTranscode) << "could not flush the copy to the disk";
    if (QFileInfo::exists(t.job.finalPath)) QFile::remove(t.job.finalPath);
    if (!QDir().mkpath(QFileInfo(t.job.finalPath).path()) ||
        !QFile::rename(t.job.partPath, t.job.finalPath)) {
      finish(t.job.key, TransferOutcome::DiskError);
      return;
    }
    t.total = t.partBytes;
    emitProgress(t, true);
    finish(t.job.key, TransferOutcome::Complete);
  }

  void DownloadWorker::onHlsSegment(Transfer& t, const QByteArray& body) {
    if (body.isEmpty()) {
      // A segment is never empty: the server gave up on it.
      finish(t.job.key, TransferOutcome::NetworkError);
      return;
    }
    // Whole segments only: written at the end of the last whole one, and
    // anything after it (a write cut by a crash) cut off.
    if (!t.file.open(QIODevice::ReadWrite) || !t.file.seek(t.partBytes) ||
        t.file.write(body) != body.size() || !t.file.flush() ||
        !t.file.resize(t.partBytes + body.size())) {
      t.file.close();
      finish(t.job.key, writeFailure(m_guard.get(), t.job.partPath));
      return;
    }
    t.file.close();
    t.partBytes += body.size();
    t.received += body.size();
    ++t.nextSegment;
    Q_EMIT segmentWritten(t.job.key, t.nextSegment, t.partBytes);
    // Progress: the bytes so far against what the segments so far suggest
    // for the whole copy.
    const auto count = static_cast<std::int64_t>(t.playlist.segments.size());
    t.total = t.partBytes * count / t.nextSegment;
    emitProgress(t, false);
    if (t.received >= t.nextCheck) {
      t.nextCheck = t.received + m_checkEvery;
      if (m_guard && m_guard->check(QFileInfo(t.job.partPath).path()) != StorageVerdict::Ok) {
        finish(t.job.key, TransferOutcome::LowSpace);
        return;
      }
    }
    hlsNextSegment(t);
  }

  void DownloadWorker::stopEncoding(const Transfer& t) {
    if (t.playSessionId.isEmpty() || t.deviceId.isEmpty() || !m_network) return;
    const QUrl url = api::activeEncodingsUrl(t.job.serverUrl, t.deviceId, t.playSessionId);
    if (!api::isServerUrl(t.job.serverUrl, url)) return;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", t.job.authorization);
    request.setTransferTimeout(10'000);
    // Nobody waits for the answer: the server also ends an idle job itself.
    QNetworkReply* reply = m_network->deleteResource(request);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
  }

} // namespace jfd::offline
