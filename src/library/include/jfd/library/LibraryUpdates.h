#pragma once

#include <jfd/api/Models.h>

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QStringList>

namespace jfd::api {
  class SessionSocket;
}

namespace jfd::library {

  // Changes the server pushes over the session WebSocket, for the screens:
  // watched state, resume position and favorites (`UserDataChanged`), and
  // items added, changed or removed (`LibraryChanged`). Changes the client
  // makes itself (a favorite toggled on a page) go through `publish` too, so
  // every model shows them at once, before the server's echo arrives.
  class LibraryUpdates : public QObject {
    Q_OBJECT

  public:
    explicit LibraryUpdates(QObject* parent = nullptr);

    // Listens to `socket` until it is destroyed; one socket at a time.
    void attach(api::SessionSocket& socket);

    void publish(const api::UserItemData& data);

    // Exposed for tests: handles one socket message.
    void handleMessage(const QString& messageType, const QJsonValue& data);

  Q_SIGNALS:
    void userDataChanged(const jfd::api::UserItemData& data);
    // Ids the server reported; empty lists are left out of the message.
    void libraryChanged(const QStringList& added, const QStringList& updated,
                        const QStringList& removed);
    // The connection came back: anything may have changed while it was down.
    void resynchronize();

  private:
    QMetaObject::Connection m_message;
    QMetaObject::Connection m_ready;
  };

} // namespace jfd::library
