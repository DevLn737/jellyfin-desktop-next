#pragma once

#include <jfd/api/LiveTv.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include <QUrl>

#include <functional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  // One channel's page (route "channel/<id>"): the channel and its
  // programmes from what is on now to `Options::hours` ahead. Programmes
  // that end drop off the top as time passes.
  class LiveTvSchedule : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString channelId READ channelId WRITE setChannelId NOTIFY channelChanged)
    Q_PROPERTY(QString name READ name NOTIFY channelChanged)
    Q_PROPERTY(QString number READ number NOTIFY channelChanged)
    Q_PROPERTY(QUrl imageUrl READ imageUrl NOTIFY channelChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      TitleRole,
      EpisodeTitleRole,
      OverviewRole,
      // Local times.
      StartRole,
      EndRole,
      OnNowRole,
      // 0..1; 0 for what has not started.
      ProgressRole,
      IsSeriesRole,
      // What records it, if anything.
      TimerIdRole,
      SeriesTimerIdRole,
    };
    Q_ENUM(Role)

    struct Options {
      int hours = 24;
      int programPageSize = 500;
      int tickMs = 30'000;
      int imageWidth = 240;
      qreal devicePixelRatio = 1.0;
      // The wall clock; tests set their own.
      std::function<QDateTime()> now;
    };

    LiveTvSchedule(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit LiveTvSchedule(api::JellyfinClient& client, QObject* parent = nullptr)
        : LiveTvSchedule(client, Options{}, parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString channelId() const { return m_channelId; }
    // Loads the channel and its programmes.
    void setChannelId(const QString& channelId);
    QString name() const { return m_channel.name; }
    QString number() const { return m_channel.number; }
    QUrl imageUrl() const { return m_imageUrl; }
    int count() const { return static_cast<int>(m_programs.size()); }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_errorText; }
    const QList<api::LiveTvProgram>& programs() const { return m_programs; }

    Q_INVOKABLE void reload();
    // Marks the programmes with the server's timers (as
    // LiveTvGuide::applyTimers).
    void applyTimers(const QList<api::LiveTvTimer>& timers);
    // Drops what ended and moves the progress on (the timer does it too).
    Q_INVOKABLE void tick();

  Q_SIGNALS:
    void channelChanged();
    void countChanged();
    void loadingChanged();
    void errorTextChanged();

  private:
    QDateTime now() const;
    void setPrograms(QList<api::LiveTvProgram> programs);
    void fail(const api::ApiError& error);
    void setLoading(bool loading);
    void setError(const QString& text);

    api::JellyfinClient& m_client;
    api::LiveTvApi m_api;
    Options m_options;
    QString m_channelId;
    api::LiveTvChannel m_channel;
    QUrl m_imageUrl;
    QList<api::LiveTvProgram> m_programs;
    QTimer m_tick;
    quint64 m_serial = 0;
    bool m_loading = false;
    QString m_errorText;
  };

} // namespace jfd::library
