#pragma once

#include <jfd/api/LiveTv.h>

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  // The recordings on the server (the "Recordings" screen, route
  // "recordings"): those recording now first, then the finished ones,
  // newest first. A finished recording is a library item and plays like
  // one (with its time line); one in progress is stopped through its timer
  // (LiveTvRecorder::cancel), not deleted.
  class LiveTvRecordings : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      // The series for an episode, else the name.
      TitleRole,
      // The episode's name; empty for anything else.
      SubtitleRole,
      OverviewRole,
      ChannelNameRole,
      // Local: when it started recording (the programme's start, or the
      // file's date).
      DateRole,
      RunTimeTicksRole,
      PositionTicksRole,
      PlayedRole,
      InProgressRole,
      // The timer of one in progress.
      TimerIdRole,
      ImageUrlRole,
    };
    Q_ENUM(Role)

    struct Options {
      int pageSize = 200;
      int imageWidth = 240;
      qreal devicePixelRatio = 1.0;
    };

    LiveTvRecordings(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit LiveTvRecordings(api::JellyfinClient& client, QObject* parent = nullptr)
        : LiveTvRecordings(client, Options{}, parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_errorText; }
    const QList<api::LiveTvRecording>& recordings() const { return m_rows; }

    // Reads every recording again (page after page).
    Q_INVOKABLE void reload();
    // Deletes a finished recording and its file; the row goes once the
    // server agrees. Emits removed() or failed().
    Q_INVOKABLE void remove(const QString& recordingId);

  Q_SIGNALS:
    void countChanged();
    void loadingChanged();
    void errorTextChanged();
    void removed(const QString& recordingId);
    // A delete failed; the text says why, for the shell's toast.
    void failed(const QString& text);

  private:
    void loadPage(quint64 serial, int startIndex, QList<api::LiveTvRecording> got);
    void setRows(QList<api::LiveTvRecording> rows);
    void setLoading(bool loading);
    void setError(const QString& text);

    api::JellyfinClient& m_client;
    api::LiveTvApi m_api;
    Options m_options;
    QList<api::LiveTvRecording> m_rows;
    quint64 m_serial = 0;
    bool m_loading = false;
    QString m_errorText;
  };

} // namespace jfd::library
