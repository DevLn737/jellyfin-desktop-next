#pragma once

#include <jfd/api/LiveTv.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  // The Live TV channel list (route "livetv"): every channel with what is on
  // now, how far it is, and what comes next. Channels come from
  // LiveTv/Channels (all pages), now and next from one LiveTv/Programs window
  // starting now; a channel the guide does not cover still lists, without a
  // programme. As time passes the rows move on by themselves, from the
  // programmes already loaded; the window is fetched again once it runs out.
  //
  // `available` says whether the server has Live TV for this user
  // (LiveTv/Info); the sidebar entry follows it.
  class LiveTvChannels : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      NameRole,
      NumberRole,
      ImageUrlRole,
      FavoriteRole,
      // What is on now; empty when the guide does not know.
      NowTitleRole,
      NowStartRole,
      NowEndRole,
      // 0..1.
      NowProgressRole,
      NextTitleRole,
      NextStartRole,
    };
    Q_ENUM(Role)

    struct Options {
      // Hours of guide loaded for "next".
      int windowHours = 3;
      int channelPageSize = 200;
      int programPageSize = 500;
      // How often the progress and now/next move on.
      int tickMs = 30'000;
      int imageWidth = 120;
      qreal devicePixelRatio = 1.0;
      // The wall clock; tests set their own.
      std::function<QDateTime()> now;
    };

    struct Row {
      api::LiveTvChannel channel;
      QUrl imageUrl;
      // The guide for this channel from now on, by start.
      QList<api::LiveTvProgram> programs;
    };

    LiveTvChannels(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit LiveTvChannels(api::JellyfinClient& client, QObject* parent = nullptr)
        : LiveTvChannels(client, Options{}, parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_errorText; }
    bool available() const { return m_available; }
    const Row* row(int index) const;

    // The programme on `row`'s channel at `at`, and the one after it.
    std::optional<api::LiveTvProgram> nowOn(int row, const QDateTime& at) const;
    std::optional<api::LiveTvProgram> nextOn(int row, const QDateTime& at) const;

    // Loads the channels and the guide window again.
    Q_INVOKABLE void reload();
    // Asks LiveTv/Info whether Live TV is there for the signed-in user.
    Q_INVOKABLE void checkAvailable();
    // Moves the rows on to the current time (the timer does it too).
    Q_INVOKABLE void tick();
    // Signed out: forgets everything.
    void clear();

  Q_SIGNALS:
    void countChanged();
    void loadingChanged();
    void errorTextChanged();
    void availableChanged();

  private:
    QDateTime now() const;
    void loadChannels(quint64 serial, int startIndex, QList<api::LiveTvChannel> channels);
    void loadPrograms(quint64 serial, int startIndex, QList<api::LiveTvChannel> channels,
                      QList<api::LiveTvProgram> programs);
    void finish(QList<api::LiveTvChannel> channels, QList<api::LiveTvProgram> programs);
    void fail(const api::ApiError& error);
    void setLoading(bool loading);
    void setError(const QString& text);
    void setAvailable(bool available);

    api::JellyfinClient& m_client;
    api::LiveTvApi m_api;
    Options m_options;
    QList<Row> m_rows;
    QTimer m_tick;
    // The end of the loaded guide window.
    QDateTime m_windowEnd;
    quint64 m_serial = 0;
    quint64 m_infoSerial = 0;
    bool m_loading = false;
    bool m_available = false;
    QString m_errorText;
  };

} // namespace jfd::library
