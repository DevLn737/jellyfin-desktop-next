#include <jfd/offline/ViewJournal.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <cstdlib>

namespace jfd::offline {

  namespace {
    constexpr int FormatVersion = 1;
    constexpr std::int64_t TicksPerSecond = 10'000'000;

    double number(std::int64_t value) { return static_cast<double>(value); }
    std::int64_t integer(const QJsonValue& value) {
      const double d = value.toDouble();
      return d > -9007199254740992.0 && d < 9007199254740992.0 ? static_cast<std::int64_t>(d) : 0;
    }

    QJsonObject eventJson(const ViewEvent& e) {
      return {{QStringLiteral("v"), FormatVersion},
              {QStringLiteral("type"), QStringLiteral("event")},
              {QStringLiteral("serverId"), e.serverId},
              {QStringLiteral("userId"), e.userId},
              {QStringLiteral("itemId"), e.itemId},
              {QStringLiteral("playId"), e.playId},
              {QStringLiteral("positionTicks"), number(e.positionTicks)},
              {QStringLiteral("stopped"), e.stopped},
              {QStringLiteral("atMs"), number(e.atMs)},
              {QStringLiteral("clockOffsetMs"), number(e.clockOffsetMs)},
              {QStringLiteral("clockKnown"), e.clockKnown}};
    }

    QJsonObject updateJson(const ViewUpdate& u) {
      return {{QStringLiteral("v"), FormatVersion},
              {QStringLiteral("type"), QStringLiteral("update")},
              {QStringLiteral("serverId"), u.serverId},
              {QStringLiteral("userId"), u.userId},
              {QStringLiteral("itemId"), u.itemId},
              {QStringLiteral("body"), u.body},
              {QStringLiteral("atMs"), number(u.atMs)}};
    }

    QJsonObject clockJson(std::int64_t offset) {
      return {{QStringLiteral("v"), FormatVersion},
              {QStringLiteral("type"), QStringLiteral("clock")},
              {QStringLiteral("offsetMs"), number(offset)}};
    }

    QByteArray line(const QJsonObject& object) {
      return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    }

    bool sameUser(const QString& serverId, const QString& userId, const QString& s,
                  const QString& u) {
      return serverId == s && userId == u;
    }
  } // namespace

  ViewJournal::ViewJournal(QString path) : m_path(std::move(path)) { load(); }

  void ViewJournal::load() {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) return;
    while (!file.atEnd()) {
      const QJsonDocument document = QJsonDocument::fromJson(file.readLine());
      if (!document.isObject()) continue;
      const QJsonObject o = document.object();
      if (o.value(QLatin1String("v")).toInt() != FormatVersion) continue;
      const QString type = o.value(QLatin1String("type")).toString();
      if (type == QLatin1String("clock")) {
        m_clockOffsetMs = integer(o.value(QLatin1String("offsetMs")));
        continue;
      }
      const QString serverId = o.value(QLatin1String("serverId")).toString();
      const QString userId = o.value(QLatin1String("userId")).toString();
      const QString itemId = o.value(QLatin1String("itemId")).toString();
      if (serverId.isEmpty() || userId.isEmpty() || itemId.isEmpty()) continue;
      if (type == QLatin1String("event")) {
        ViewEvent e;
        e.serverId = serverId;
        e.userId = userId;
        e.itemId = itemId;
        e.playId = o.value(QLatin1String("playId")).toString();
        e.positionTicks = integer(o.value(QLatin1String("positionTicks")));
        e.stopped = o.value(QLatin1String("stopped")).toBool();
        e.atMs = integer(o.value(QLatin1String("atMs")));
        e.clockOffsetMs = integer(o.value(QLatin1String("clockOffsetMs")));
        e.clockKnown = o.value(QLatin1String("clockKnown")).toBool();
        if (!e.playId.isEmpty()) m_events.append(e);
      } else if (type == QLatin1String("update")) {
        ViewUpdate u;
        u.serverId = serverId;
        u.userId = userId;
        u.itemId = itemId;
        u.body = o.value(QLatin1String("body")).toObject();
        u.atMs = integer(o.value(QLatin1String("atMs")));
        // A later line for the item replaces an earlier one.
        m_updates.removeIf([&u](const ViewUpdate& other) {
          return other.itemId == u.itemId && sameUser(u.serverId, u.userId, other.serverId, other.userId);
        });
        m_updates.append(u);
      }
    }
  }

  bool ViewJournal::rewrite() {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QByteArray out;
    if (m_clockOffsetMs) out += line(clockJson(*m_clockOffsetMs));
    for (const ViewUpdate& u : std::as_const(m_updates))
      out += line(updateJson(u));
    for (const ViewEvent& e : std::as_const(m_events))
      out += line(eventJson(e));
    return file.write(out) == out.size() && file.commit();
  }

  bool ViewJournal::append(const ViewEvent& event) {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    const QByteArray bytes = line(eventJson(event));
    if (file.write(bytes) != bytes.size() || !file.flush()) return false;
    m_events.append(event);
    return true;
  }

  QList<ViewEvent> ViewJournal::events(const QString& serverId, const QString& userId) const {
    QList<ViewEvent> out;
    for (const ViewEvent& e : m_events)
      if (sameUser(serverId, userId, e.serverId, e.userId)) out.append(e);
    return out;
  }

  QList<ViewUpdate> ViewJournal::updates(const QString& serverId, const QString& userId) const {
    QList<ViewUpdate> out;
    for (const ViewUpdate& u : m_updates)
      if (sameUser(serverId, userId, u.serverId, u.userId)) out.append(u);
    return out;
  }

  std::optional<ViewUpdate> ViewJournal::update(const QString& serverId, const QString& userId,
                                                const QString& itemId) const {
    for (const ViewUpdate& u : m_updates)
      if (u.itemId == itemId && sameUser(serverId, userId, u.serverId, u.userId)) return u;
    return std::nullopt;
  }

  bool ViewJournal::hasPlay(const QString& playId) const {
    for (const ViewEvent& e : m_events)
      if (e.playId == playId) return true;
    return false;
  }

  bool ViewJournal::removePlay(const QString& playId) {
    if (m_events.removeIf([&playId](const ViewEvent& e) { return e.playId == playId; }) == 0)
      return true;
    return rewrite();
  }

  bool ViewJournal::setUpdate(const ViewUpdate& update, const QStringList& consumedPlays) {
    m_updates.removeIf([&update](const ViewUpdate& u) {
      return u.itemId == update.itemId && sameUser(update.serverId, update.userId, u.serverId, u.userId);
    });
    m_updates.append(update);
    m_events.removeIf([&consumedPlays](const ViewEvent& e) { return consumedPlays.contains(e.playId); });
    return rewrite();
  }

  bool ViewJournal::removeItem(const QString& serverId, const QString& userId, const QString& itemId,
                               const QStringList& plays) {
    m_updates.removeIf([&](const ViewUpdate& u) {
      return u.itemId == itemId && sameUser(serverId, userId, u.serverId, u.userId);
    });
    m_events.removeIf([&plays](const ViewEvent& e) { return plays.contains(e.playId); });
    return rewrite();
  }

  void ViewJournal::setClockOffsetMs(std::int64_t offset) {
    if (m_clockOffsetMs && std::llabs(*m_clockOffsetMs - offset) <= 1000) return;
    m_clockOffsetMs = offset;
    rewrite();
  }

  ViewOutcome classifyView(std::int64_t positionTicks, std::int64_t runTimeTicks) {
    if (positionTicks <= 0) return ViewOutcome::Nothing;
    // Jellyfin counts an item without a known length as played.
    if (runTimeTicks <= 0) return ViewOutcome::Played;
    const double percent = 100.0 * static_cast<double>(positionTicks) / static_cast<double>(runTimeTicks);
    if (percent < 5.0) return ViewOutcome::Nothing;
    if (percent > 90.0 || positionTicks >= runTimeTicks) return ViewOutcome::Played;
    if (runTimeTicks < 300 * TicksPerSecond) return ViewOutcome::Played;
    return ViewOutcome::Resume;
  }

} // namespace jfd::offline
