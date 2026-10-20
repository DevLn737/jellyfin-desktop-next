#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace jfd::library {

  // A server the user has signed in to. The access token is not here: it
  // lives in the system credential store under platform::accessTokenKey.
  struct SavedServer {
    QString id;
    QString name;
    QUrl url;
    // The signed-in user, empty after signing out.
    QString userId;
    QString userName;
    QDateTime lastUsed;
  };

  // The list of servers, most recently used first, and which one is active
  // (only one at a time). Kept in an INI file in the user's profile.
  class ServerStore {
  public:
    explicit ServerStore(QString iniPath);

    QList<SavedServer> servers() const;
    std::optional<SavedServer> server(const QString& id) const;
    std::optional<SavedServer> active() const;

    // Adds or replaces the server with this id and makes it the active one.
    void save(const SavedServer& server);
    void setActive(const QString& id);
    // Forgets the signed-in user but keeps the server in the list.
    void clearUser(const QString& id);
    void remove(const QString& id);

  private:
    void write(const QList<SavedServer>& servers, const QString& active);
    QString activeId() const;

    QString m_path;
  };

} // namespace jfd::library
