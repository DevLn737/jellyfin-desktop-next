#pragma once

#include <jfd/integrations/DiscordActivity.h>
#include <jfd/integrations/DiscordIpc.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

// Discord Rich Presence for what plays, off by default (a setting). Keeps the
// card's timer steady across pauses and refreshes, and rebases it on seeks and
// new items.
namespace jfd::integrations {

  class DiscordPresence final : public QObject {
    Q_OBJECT
  public:
    // The public Application ID of the "Jellyfin Desktop" application, the
    // one the 2.x client uses.
    static QString defaultApplicationId() { return QStringLiteral("1557831977250463766"); }

    explicit DiscordPresence(QStringList endpoints = {}, QObject* parent = nullptr);

    void configure(bool enabled, const QString& applicationId = defaultApplicationId());
    // nullopt: nothing plays; the card is removed.
    void update(const std::optional<PresenceInfo>& info);

    // Seconds since the epoch; replaced in tests.
    void setClock(std::function<qint64()> nowSeconds) { m_now = std::move(nowSeconds); }

  private:
    void publish();

    DiscordIpc m_ipc;
    QTimer m_refresh;
    std::optional<PresenceInfo> m_info;
    QElapsedTimer m_positionClock;
    qint64 m_start = 0;
    std::function<qint64()> m_now = [] { return QDateTime::currentSecsSinceEpoch(); };
  };

} // namespace jfd::integrations
