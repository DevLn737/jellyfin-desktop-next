#include <jfd/library/ServerStore.h>

#include <QSettings>

#include <algorithm>

namespace jfd::library {

  namespace {
    const QString Group = QStringLiteral("servers");
    const QString Active = QStringLiteral("activeServer");
  } // namespace

  ServerStore::ServerStore(QString iniPath) : m_path(std::move(iniPath)) {}

  QList<SavedServer> ServerStore::servers() const {
    QSettings ini(m_path, QSettings::IniFormat);
    QList<SavedServer> out;
    const int count = ini.beginReadArray(Group);
    for (int i = 0; i < count; ++i) {
      ini.setArrayIndex(i);
      SavedServer s;
      s.id = ini.value(QStringLiteral("id")).toString();
      s.name = ini.value(QStringLiteral("name")).toString();
      s.url = QUrl(ini.value(QStringLiteral("url")).toString());
      s.userId = ini.value(QStringLiteral("userId")).toString();
      s.userName = ini.value(QStringLiteral("userName")).toString();
      s.lastUsed = QDateTime::fromString(ini.value(QStringLiteral("lastUsed")).toString(),
                                         Qt::ISODateWithMs);
      if (!s.id.isEmpty() && s.url.isValid()) out.append(s);
    }
    ini.endArray();
    std::stable_sort(out.begin(), out.end(), [](const SavedServer& a, const SavedServer& b) {
      return a.lastUsed > b.lastUsed;
    });
    return out;
  }

  std::optional<SavedServer> ServerStore::server(const QString& id) const {
    for (const SavedServer& s : servers())
      if (s.id == id) return s;
    return std::nullopt;
  }

  QString ServerStore::activeId() const {
    return QSettings(m_path, QSettings::IniFormat).value(Active).toString();
  }

  std::optional<SavedServer> ServerStore::active() const {
    const QString id = activeId();
    return id.isEmpty() ? std::nullopt : server(id);
  }

  void ServerStore::write(const QList<SavedServer>& list, const QString& active) {
    QSettings ini(m_path, QSettings::IniFormat);
    ini.remove(Group);
    ini.beginWriteArray(Group, static_cast<int>(list.size()));
    for (int i = 0; i < list.size(); ++i) {
      const SavedServer& s = list[i];
      ini.setArrayIndex(i);
      ini.setValue(QStringLiteral("id"), s.id);
      ini.setValue(QStringLiteral("name"), s.name);
      ini.setValue(QStringLiteral("url"), s.url.toString());
      ini.setValue(QStringLiteral("userId"), s.userId);
      ini.setValue(QStringLiteral("userName"), s.userName);
      ini.setValue(QStringLiteral("lastUsed"), s.lastUsed.toString(Qt::ISODateWithMs));
    }
    ini.endArray();
    if (active.isEmpty())
      ini.remove(Active);
    else
      ini.setValue(Active, active);
    ini.sync();
  }

  void ServerStore::save(const SavedServer& server) {
    QList<SavedServer> list = servers();
    list.removeIf([&](const SavedServer& s) { return s.id == server.id; });
    SavedServer copy = server;
    if (!copy.lastUsed.isValid()) copy.lastUsed = QDateTime::currentDateTimeUtc();
    list.prepend(copy);
    write(list, server.id);
  }

  void ServerStore::setActive(const QString& id) {
    QList<SavedServer> list = servers();
    for (SavedServer& s : list)
      if (s.id == id) s.lastUsed = QDateTime::currentDateTimeUtc();
    write(list, id);
  }

  void ServerStore::clearUser(const QString& id) {
    QList<SavedServer> list = servers();
    for (SavedServer& s : list)
      if (s.id == id) {
        s.userId.clear();
        s.userName.clear();
      }
    write(list, activeId());
  }

  void ServerStore::remove(const QString& id) {
    QList<SavedServer> list = servers();
    list.removeIf([&](const SavedServer& s) { return s.id == id; });
    const QString active = activeId();
    write(list, active == id ? QString() : active);
  }

} // namespace jfd::library
