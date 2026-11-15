#pragma once

#include <jfd/api/LiveTv.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  // The programme guide (route "guide"): one row per channel, its
  // programmes along a time axis that starts at the half hour before now
  // and runs `Options::hours` on.
  //
  // Times are minutes from rangeStart. The screen shows a part of the axis
  // (viewStartMinutes, viewMinutes); a row's `programs` lists only the
  // programmes in that part and an hour either side, so a row makes a
  // handful of cells whatever the length of the guide, and the channel list
  // (a ListView) only the rows on screen. The guide loads in chunks of
  // `chunkHours` for every channel as the view reaches them.
  //
  // Focus is the model's, so arrows and a gamepad's d-pad move the same way
  // and the tests can drive it: a row and a time (focusMinutes). Up and
  // Down keep the time and land on the programme on the air then; Left and
  // Right step to the programme before or after; paging moves the time a
  // screen at a time. The view follows the focus.
  class LiveTvGuide : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    // Local time of minute 0, and the axis' length.
    Q_PROPERTY(QDateTime rangeStart READ rangeStartLocal NOTIFY rangeChanged)
    Q_PROPERTY(int rangeMinutes READ rangeMinutes NOTIFY rangeChanged)
    // Where now is on the axis; moves on with the clock.
    Q_PROPERTY(double nowMinutes READ nowMinutes NOTIFY nowChanged)
    // The part of the axis on screen. The page sets the width it has room
    // for; the start follows the focus and the user's scrolling.
    Q_PROPERTY(int viewStartMinutes READ viewStartMinutes WRITE setViewStartMinutes NOTIFY viewChanged)
    Q_PROPERTY(int viewMinutes READ viewMinutes WRITE setViewMinutes NOTIFY viewChanged)
    Q_PROPERTY(int focusRow READ focusRow NOTIFY focusChanged)
    Q_PROPERTY(int focusMinutes READ focusMinutes NOTIFY focusChanged)
    // The focused programme: id, title, episodeTitle, overview, offset,
    // length (minutes), start, end (local), channelId, channelName. Empty
    // where the guide has nothing (the channel still has an id).
    Q_PROPERTY(QVariantMap focusedProgram READ focusedProgram NOTIFY focusChanged)
    Q_PROPERTY(QString focusedChannelId READ focusedChannelId NOTIFY focusChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      NameRole,
      NumberRole,
      ImageUrlRole,
      // The programmes in view (and an hour either side), by start: maps
      // with id, title, offset and length in minutes.
      ProgramsRole,
    };
    Q_ENUM(Role)

    struct Options {
      // The length of the axis.
      int hours = 24;
      // How much of the guide one request asks for.
      int chunkHours = 6;
      int channelPageSize = 200;
      int programPageSize = 500;
      // How often "now" moves on.
      int tickMs = 30'000;
      int imageWidth = 80;
      qreal devicePixelRatio = 1.0;
      // The wall clock; tests set their own.
      std::function<QDateTime()> now;
    };

    LiveTvGuide(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit LiveTvGuide(api::JellyfinClient& client, QObject* parent = nullptr)
        : LiveTvGuide(client, Options{}, parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_errorText; }
    QDateTime rangeStart() const { return m_rangeStart; }
    QDateTime rangeStartLocal() const { return m_rangeStart.toLocalTime(); }
    int rangeMinutes() const { return m_options.hours * 60; }
    double nowMinutes() const;
    int viewStartMinutes() const { return m_viewStart; }
    void setViewStartMinutes(int minutes);
    int viewMinutes() const { return m_viewMinutes; }
    void setViewMinutes(int minutes);
    int focusRow() const { return m_focusRow; }
    int focusMinutes() const { return m_focusMinutes; }
    QVariantMap focusedProgram() const;
    QString focusedChannelId() const;

    // The loaded guide of a row, by start.
    QList<api::LiveTvProgram> programsAt(int row) const;
    // The programme on `row` at `minutes`, if the guide has one.
    std::optional<api::LiveTvProgram> programAt(int row, int minutes) const;
    // Requests made for the guide so far (for the tests: chunks load once).
    int chunksRequested() const { return m_chunksRequested; }

    // Loads the channels and the guide around now again; the focus goes to
    // now on the first row (or stays on its channel).
    Q_INVOKABLE void reload();
    // An arrow: dx steps along the row (-1, 1), dy across the rows. False
    // at an edge (nothing moved).
    Q_INVOKABLE bool moveFocus(int dx, int dy);
    // A screen's worth of time earlier (-1) or later (1).
    Q_INVOKABLE bool pageFocus(int direction);
    Q_INVOKABLE void focusAt(int row, int minutes);
    Q_INVOKABLE void focusNow();
    // Moves now and the cells on with the clock (the timer does it too).
    Q_INVOKABLE void tick();
    void clear();

  Q_SIGNALS:
    void countChanged();
    void loadingChanged();
    void errorTextChanged();
    void rangeChanged();
    void nowChanged();
    void viewChanged();
    void focusChanged();

  private:
    struct Row {
      api::LiveTvChannel channel;
      QUrl imageUrl;
      QList<api::LiveTvProgram> programs;
    };
    enum class Chunk { Missing, Loading, Loaded };

    QDateTime now() const;
    double minutesOf(const QDateTime& when) const;
    void loadChunks(int fromMinutes, int toMinutes);
    void loadChunk(int chunk);
    void addPrograms(const QList<api::LiveTvProgram>& programs);
    // The window `programs` lists, in whole half hours around the view.
    std::pair<int, int> window() const;
    void viewMoved();
    void followFocus();
    void setFocus(int row, int minutes, bool snapToStart);
    void fail(const api::ApiError& error);
    void setLoading(bool loading);
    void setError(const QString& text);

    api::JellyfinClient& m_client;
    api::LiveTvApi m_api;
    Options m_options;
    QList<Row> m_rows;
    QHash<QString, int> m_rowOf;
    QList<Chunk> m_chunks;
    QDateTime m_rangeStart;
    QTimer m_tick;
    int m_viewStart = 0;
    int m_viewMinutes = 180;
    std::pair<int, int> m_window{0, 0};
    int m_focusRow = 0;
    int m_focusMinutes = 0;
    QString m_focusChannel;
    quint64 m_serial = 0;
    int m_pendingChunks = 0;
    int m_chunksRequested = 0;
    bool m_loading = false;
    QString m_errorText;
  };

} // namespace jfd::library
