#include <jfd/session/ApiTransport.h>

#include <jfd/api/JellyfinClient.h>

#include <utility>

namespace jfd::session {

  ApiTransport::ApiTransport(api::JellyfinClient& client, QObject* parent)
      : QObject(parent), m_client(client) {}

  void ApiTransport::request(const syncplay::HttpRequest& request, Reply reply) {
    using Method = api::JellyfinClient::Method;
    const Method method =
        request.method == syncplay::HttpRequest::Method::Get ? Method::Get : Method::Post;
    m_client.rawRequest(method, QString::fromStdString(request.path),
                        QByteArray::fromStdString(request.body), this,
                        [reply = std::move(reply)](int status, QByteArray body) {
                          reply(status, body.toStdString());
                        });
  }

} // namespace jfd::session
