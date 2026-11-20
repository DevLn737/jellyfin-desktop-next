#pragma once

#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/OfflineCatalog.h>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// The small files a downloaded item needs to play and show without the
// server, fetched once its media file is complete:
//
//   <media name>.<lang>.<srt|ass|ssa|vtt>
//                                   external text subtitles beside the media
//                                   in the library (format 2; subs/<index>.
//                                   <ext> in the item folder for format 1).
//                                   Embedded ones, and the fonts of embedded
//                                   ASS, are in the media file of an original
//   images/<Type>                   Primary, Backdrop (the first), Logo and
//                                   Thumb, at card and backdrop size
//   lyrics.json                     a track's lyrics (Audio/{id}/Lyrics)
//   segments.json                   a video's intros and credits
//
// They are a few hundred kilobytes, so they are fetched on the GUI thread,
// one at a time, with the session's Authorization header and only from the
// server's own address (redirects are not followed). A 404 means the server
// has none; a network error or 5xx leaves the item for the next try
// (fetchMissing, at sign-in), its media still playable.
namespace jfd::offline {

  class ExtrasDownloader : public QObject {
    Q_OBJECT

  public:
    ExtrasDownloader(DownloadStore& store, OfflineCatalog& catalog, QObject* parent = nullptr);
    ~ExtrasDownloader() override;

    // As DownloadQueue::setAccess. An empty serverUrl stops.
    void setAccess(const QString& serverId, const QString& userId, const QUrl& serverUrl,
                   const QByteArray& authorization);

    // Fetches what a complete item of the signed-in user lacks.
    void fetch(const QString& itemId);
    // Every complete item of the signed-in user without its extras.
    void fetchMissing();

    bool busy() const { return !m_current.isEmpty(); }

  Q_SIGNALS:
    // The item's manifest was updated; `complete` is its extrasComplete.
    void finished(const QString& itemId, bool complete);

  private:
    struct Task {
      QUrl url;
      // Relative to the item folder.
      QString file;
      enum class Kind { Subtitle, Image, Lyrics, Segments } kind = Kind::Image;
      // Subtitle stream index or image type.
      int streamIndex = -1;
      QString imageType;
    };

    QList<Task> tasksFor(const ItemManifest& manifest) const;
    void next();
    void startTask();
    void onReply(QNetworkReply* reply);
    void finishItem(bool complete);

    DownloadStore& m_store;
    OfflineCatalog& m_catalog;
    QNetworkAccessManager* m_network = nullptr;

    QString m_serverId;
    QString m_userId;
    QUrl m_serverUrl;
    QByteArray m_authorization;

    QStringList m_pending;
    QString m_current;
    QList<Task> m_tasks;
    ItemManifest m_manifest;
    bool m_failed = false;
    QPointer<QNetworkReply> m_reply;
  };

} // namespace jfd::offline
