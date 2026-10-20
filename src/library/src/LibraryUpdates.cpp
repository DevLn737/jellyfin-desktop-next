#include <jfd/library/LibraryUpdates.h>

#include <jfd/api/SessionSocket.h>

#include <QJsonArray>
#include <QJsonObject>

namespace jfd::library {

  namespace {
    QStringList ids(const QJsonObject& o, const char* key) {
      QStringList out;
      for (const auto& value : o.value(QLatin1String(key)).toArray())
        if (value.isString()) out.append(value.toString());
      return out;
    }
  } // namespace

  LibraryUpdates::LibraryUpdates(QObject* parent) : QObject(parent) {}

  void LibraryUpdates::attach(api::SessionSocket& socket) {
    disconnect(m_message);
    disconnect(m_ready);
    m_message = connect(&socket, &api::SessionSocket::messageReceived, this,
                        [this](const QString& type, const QJsonValue& data, const QString&) {
                          handleMessage(type, data);
                        });
    // The first Ready is the initial connection; later ones are reconnects.
    m_ready = connect(&socket, &api::SessionSocket::ready, this, [this](quint64 generation) {
      if (generation > 1) Q_EMIT resynchronize();
    });
  }

  void LibraryUpdates::publish(const api::UserItemData& data) {
    if (!data.itemId.isEmpty()) Q_EMIT userDataChanged(data);
  }

  void LibraryUpdates::handleMessage(const QString& messageType, const QJsonValue& data) {
    const QJsonObject o = data.toObject();
    if (messageType == QLatin1String("UserDataChanged")) {
      for (const auto& value : o.value(QLatin1String("UserDataList")).toArray()) {
        auto parsed = api::json::userItemData(value.toObject());
        if (parsed) publish(parsed.value());
      }
    } else if (messageType == QLatin1String("LibraryChanged")) {
      const QStringList added = ids(o, "ItemsAdded");
      const QStringList updated = ids(o, "ItemsUpdated");
      const QStringList removed = ids(o, "ItemsRemoved");
      if (!added.isEmpty() || !updated.isEmpty() || !removed.isEmpty())
        Q_EMIT libraryChanged(added, updated, removed);
    }
  }

} // namespace jfd::library
