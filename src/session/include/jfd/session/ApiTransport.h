#pragma once

#include <jfd/syncplay/GroupClient.h>

#include <QObject>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::session {

  // The SyncPlay group client's HTTP requests on the signed-in Jellyfin
  // client. Replies arrive on this object's thread; none arrive after it is
  // destroyed.
  class ApiTransport final : public QObject, public syncplay::Transport {
    Q_OBJECT

  public:
    explicit ApiTransport(api::JellyfinClient& client, QObject* parent = nullptr);

    void request(const syncplay::HttpRequest& request, Reply reply) override;

  private:
    api::JellyfinClient& m_client;
  };

} // namespace jfd::session
