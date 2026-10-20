#pragma once

#include <QByteArray>
#include <QString>

// How this client introduces itself to the server. The same values go into
// every HTTP request and the session WebSocket, so the server sees one
// session per device, as the contract's sessionWebSocket rule requires.
namespace jfd::api {

  struct ClientIdentity {
    QString client = QStringLiteral("Jellyfin Desktop Next");
    QString version;
    QString device;
    // Stable per installation; the server keys sessions and tokens by it.
    QString deviceId;
  };

  // Builds `MediaBrowser Client="...", Device="...", DeviceId="...",
  // Version="...", Token="..."`. The server URL-decodes each value, so values
  // are percent-encoded; Token is omitted when empty.
  QByteArray authorizationHeader(const ClientIdentity& identity, const QString& token);

  // A new random device id for a fresh installation.
  QString generateDeviceId();

} // namespace jfd::api
