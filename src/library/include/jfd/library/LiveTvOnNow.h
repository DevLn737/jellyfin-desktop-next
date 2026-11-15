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

  // The home screen's "On now" row: channels with what is on the air, as
  // cards. It has the roles of ItemListModel (itemId, name, type,
  // subtitle, imageUrl, progress, ...), so the home screen shows it like
  // any row: the programme as the title, the channel below it, how far the
  // programme is as the progress bar. A card is of type "TvChannel" and
  // tunes in. Channels whose guide has nothing now are left out.
  class LiveTvOnNow : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

  public:
    struct Options {
      // Cards in the row (the first channels in the server's order).
      int limit = 24;
      // How often the progress moves on; a programme that ended makes the
      // row load again.
      int tickMs = 60'000;
      int imageWidth = 320;
      qreal devicePixelRatio = 1.0;
      // The wall clock; tests set their own.
      std::function<QDateTime()> now;
    };

    struct Entry {
      api::LiveTvChannel channel;
      api::LiveTvProgram program;
      QUrl imageUrl;
    };

    LiveTvOnNow(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit LiveTvOnNow(api::JellyfinClient& client, QObject* parent = nullptr)
        : LiveTvOnNow(client, Options{}, parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_entries.size()); }
    const Entry* entry(int row) const;

    Q_INVOKABLE void reload();
    // Moves the progress on (the timer does it too).
    Q_INVOKABLE void tick();
    // Signed out, or Live TV went away.
    void clear();

  Q_SIGNALS:
    void countChanged();

  private:
    QDateTime now() const;
    void setEntries(QList<Entry> entries);

    api::JellyfinClient& m_client;
    api::LiveTvApi m_api;
    Options m_options;
    QList<Entry> m_entries;
    QTimer m_tick;
    quint64 m_serial = 0;
  };

} // namespace jfd::library
