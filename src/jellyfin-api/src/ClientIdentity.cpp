#include <jfd/api/ClientIdentity.h>

#include <QUrl>
#include <QUuid>

namespace jfd::api {

  namespace {
    void appendField(QByteArray& header, const char* name, const QString& value) {
      if (!header.endsWith(' ')) header += ", ";
      header += name;
      header += "=\"";
      // The server applies WebUtility.UrlDecode, which also turns '+' into a
      // space, so everything outside the unreserved set is encoded.
      header += QUrl::toPercentEncoding(value);
      header += '"';
    }
  } // namespace

  QByteArray authorizationHeader(const ClientIdentity& identity, const QString& token) {
    QByteArray header = "MediaBrowser ";
    appendField(header, "Client", identity.client);
    appendField(header, "Device", identity.device);
    appendField(header, "DeviceId", identity.deviceId);
    appendField(header, "Version", identity.version);
    if (!token.isEmpty()) appendField(header, "Token", token);
    return header;
  }

  QString generateDeviceId() {
    return QUuid::createUuid().toString(QUuid::Id128);
  }

} // namespace jfd::api
