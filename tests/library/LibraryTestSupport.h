#pragma once

#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrlQuery>

#include <memory>

// A signed-in client against an in-process fake server, for the library
// model tests.
namespace jfd::test {

  struct LibraryFixture {
    FakeServer server;
    api::JellyfinClient client{api::ClientIdentity{}};

    explicit LibraryFixture(FakeServer::Handler handler) : server(std::move(handler)) {
      client.setServerUrl(server.url());
      client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
    }

    int count(const QString& path) const {
      int n = 0;
      for (const HttpRequest& r : server.requests())
        if (r.path == path) ++n;
      return n;
    }
  };

  inline QByteArray page(const QByteArray& items, int total = -1, int start = 0) {
    QByteArray body = R"({"Items": [)" + items + "]";
    if (total >= 0) body += R"(, "TotalRecordCount": )" + QByteArray::number(total);
    body += R"(, "StartIndex": )" + QByteArray::number(start) + "}";
    return body;
  }

  inline HttpResponse ok(const QByteArray& body, int delayMs = 0) {
    return HttpResponse{200, body, "application/json", delayMs};
  }

} // namespace jfd::test
