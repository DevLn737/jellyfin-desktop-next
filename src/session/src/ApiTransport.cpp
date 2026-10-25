#include <jfd/session/ApiTransport.h>

#include <jfd/api/JellyfinClient.h>

#include <QLoggingCategory>

#include <utility>

namespace jfd::session {

  namespace {
    // SyncPlay requests and their replies, for tracing a group session.
    Q_LOGGING_CATEGORY(lcWire, "jfd.session.wire", QtWarningMsg)

    bool traced(const QString& path) { return !path.endsWith(QLatin1String("/GetUtcTime")); }
  } // namespace

  ApiTransport::ApiTransport(api::JellyfinClient& client, QObject* parent)
      : QObject(parent), m_client(client) {}

  void ApiTransport::request(const syncplay::HttpRequest& request, Reply reply) {
    using Method = api::JellyfinClient::Method;
    const Method method =
        request.method == syncplay::HttpRequest::Method::Get ? Method::Get : Method::Post;
    const QString path = QString::fromStdString(request.path);
    if (traced(path)) qCDebug(lcWire).noquote() << "->" << path << QByteArray::fromStdString(request.body);
    m_client.rawRequest(method, path, QByteArray::fromStdString(request.body), this,
                        [this, path, reply = std::move(reply)](int status, QByteArray body) {
                          if (traced(path)) qCDebug(lcWire).noquote() << "<-" << path << status;
                          Q_EMIT replied(path, status);
                          reply(status, body.toStdString());
                        });
  }

} // namespace jfd::session
