#pragma once

#include <jfd/api/Models.h>
#include <jfd/api/Playback.h>
#include <jfd/core/SessionInput.h>

#include <QJsonObject>
#include <QObject>

#include <cstdint>
#include <functional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::session {

  // Turns "play this item" into intent::Open: fetches the item (for its title
  // and media type) and its PlaybackInfo, chooses the stream, maps its
  // timeline with playback::mediaRequest, and posts the intent. Only the latest request is opened; one
  // that resolves after a newer request was made is dropped.
  class PlaybackLauncher final : public QObject {
    Q_OBJECT

  public:
    struct Request {
      QString itemId;
      std::int64_t startTicks = 0;
      // The SyncPlay queue occurrence; empty outside a group.
      core::PlaylistItemId playlistItem;
      bool startPaused = false;
      // PlaybackInfo choices; empty means the server's defaults.
      QString mediaSourceId;
      std::optional<int> audioStreamIndex;
      std::optional<int> subtitleStreamIndex;
    };

    using Post = std::function<void(core::SessionInput)>;

    PlaybackLauncher(api::JellyfinClient& client, QJsonObject deviceProfile, Post post,
                     QObject* parent = nullptr);

    void open(const Request& request);
    // Forgets any request still resolving.
    void cancel() { ++m_serial; }

    static QString titleOf(const api::Item& item);

  Q_SIGNALS:
    // Emitted right before the intent is posted, for track menus and
    // playback reporting.
    void resolved(const jfd::session::PlaybackLauncher::Request& request,
                  const jfd::api::Item& item, const jfd::api::MediaSource& source,
                  const jfd::api::PlaybackStream& stream);
    void failed(const QString& itemId, const QString& message);

  private:
    void openWithItem(std::uint64_t serial, const Request& request, const api::Item& item);

    api::JellyfinClient& m_client;
    QJsonObject m_deviceProfile;
    Post m_post;
    std::uint64_t m_serial = 0;
  };

} // namespace jfd::session
