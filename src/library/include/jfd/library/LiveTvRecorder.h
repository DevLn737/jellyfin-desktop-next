#pragma once

#include <jfd/api/LiveTv.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  // The server's DVR for the screens (QML: Library.recorder): whether this
  // user may record, recording a programme or its series, cancelling, and
  // the recordings to come as a list model (the "Recordings" screen's
  // schedule): the series timers first, by name, then the timers by start.
  //
  // `available` needs Live TV for the user (LiveTv/Info, as the Live TV
  // entry) and the right to manage it (Policy.EnableLiveTvManagement or an
  // administrator: the server refuses timers to anyone else). The guide
  // and a channel's page show the recording actions only then.
  //
  // timers() is every scheduled timer, with the programme it records; the
  // guide and a channel's page mark their programmes from it
  // (timersChanged). After a change the list is read again, so a series
  // timer's programmes are marked as the server schedules them.
  class LiveTvRecorder : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    // A record or cancel request is on its way.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      // A series timer (cancelled with cancelSeries) or a timer (cancel).
      IsSeriesRole,
      TitleRole,
      OverviewRole,
      ChannelNameRole,
      // Local times; invalid for a series timer.
      StartRole,
      EndRole,
      // Recording now.
      RecordingRole,
      // The series timer a timer belongs to.
      SeriesTimerIdRole,
    };
    Q_ENUM(Role)

    explicit LiveTvRecorder(api::JellyfinClient& client, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return m_available; }
    int count() const { return static_cast<int>(m_series.size() + m_timers.size()); }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_errorText; }
    bool busy() const { return m_busy > 0; }
    // Every scheduled timer (to come or recording), by start.
    const QList<api::LiveTvTimer>& timers() const { return m_timers; }
    const QList<api::LiveTvTimer>& seriesTimers() const { return m_series; }
    // The timer recording `programId`, or empty.
    Q_INVOKABLE QString timerFor(const QString& programId) const;

    // Asks LiveTv/Info and the user's policy whether this user may record;
    // reads the timers when so.
    Q_INVOKABLE void checkAvailable();
    // Reads the timers and series timers again.
    Q_INVOKABLE void reload();
    // Records one programme (the server's defaults: no padding, kept until
    // deleted).
    Q_INVOKABLE void record(const QString& programId);
    // Records the programme's series: every new episode on its channel.
    Q_INVOKABLE void recordSeries(const QString& programId);
    // Cancels a timer: one to come is dropped, one recording stops.
    Q_INVOKABLE void cancel(const QString& timerId);
    Q_INVOKABLE void cancelSeries(const QString& seriesTimerId);
    // Signed out: forgets everything.
    void clear();

  Q_SIGNALS:
    void availableChanged();
    void countChanged();
    void loadingChanged();
    void errorTextChanged();
    void busyChanged();
    // The timers were read again (timers()).
    void timersChanged();
    // What happened, for the shell's toast.
    void message(const QString& text, bool error);

  private:
    enum class Action { Record, RecordSeries, Cancel, CancelSeries };
    void finished(Action action, const api::Result<api::Empty>& result);
    void setTimers(QList<api::LiveTvTimer> series, QList<api::LiveTvTimer> timers);
    void setAvailable(bool available);
    void setLoading(bool loading);
    void setError(const QString& text);
    void setBusy(int delta);

    api::JellyfinClient& m_client;
    api::LiveTvApi m_api;
    QList<api::LiveTvTimer> m_series;
    QList<api::LiveTvTimer> m_timers;
    quint64 m_serial = 0;
    quint64 m_infoSerial = 0;
    // Moves on at sign-out: answers to requests from before are dropped.
    quint64 m_session = 0;
    int m_busy = 0;
    bool m_available = false;
    bool m_loading = false;
    QString m_errorText;
  };

} // namespace jfd::library
