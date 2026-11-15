#pragma once

// The state of one transfer of DownloadWorker, shared by DownloadWorker.cpp
// (original files) and DownloadWorkerHls.cpp (compressed copies).

#include <jfd/offline/DownloadWorker.h>
#include <jfd/offline/HlsPlaylist.h>

#include <QElapsedTimer>
#include <QFile>
#include <QNetworkReply>

#include <optional>

namespace jfd::offline {

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

    // A compressed copy (job.transcode): which request is out, and where
    // the segments stand.
    enum class HlsStep { PlaybackInfo, Master, Media, Init, Segment };
    HlsStep hlsStep = HlsStep::PlaybackInfo;
    // The URL of the request out (redirects resolve against it).
    QUrl hlsUrl;
    QUrl mediaPlaylistUrl;
    HlsMediaPlaylist playlist;
    // Resume after the last whole segment when the init segment matches.
    bool resumeCandidate = false;
    int nextSegment = 0;
    std::int64_t partBytes = 0;
    // From the server's TranscodingUrl, to stop its job at the end.
    QString deviceId;
    QString playSessionId;
    QJsonObject transcodeState;
  };

} // namespace jfd::offline
