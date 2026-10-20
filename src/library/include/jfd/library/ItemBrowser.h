#pragma once

#include <jfd/api/Models.h>

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

namespace jfd::api {
  class JellyfinClient;
}

// The minimal library for the prototype: find an item, step from a series to
// its seasons and episodes, and pick something to play. Screens bind to
// ItemBrowser and its model from QML.
namespace jfd::library {

  class ItemListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      NameRole,
      TypeRole,
      // "S1:E2", the production year, or empty.
      SubtitleRole,
      ImageUrlRole,
      RunTimeTicksRole,
      PlayedRole,
      PlaybackPositionTicksRole,
      IsFolderRole,
    };
    Q_ENUM(Role)

    struct Entry {
      api::Item item;
      QUrl imageUrl;
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_entries.size()); }
    const Entry* entry(int row) const;
    void setEntries(QList<Entry> entries);

  Q_SIGNALS:
    void countChanged();

  private:
    QList<Entry> m_entries;
  };

  class ItemBrowser : public QObject {
    Q_OBJECT
    Q_PROPERTY(jfd::library::ItemListModel* model READ model CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

  public:
    explicit ItemBrowser(api::JellyfinClient& client, QObject* parent = nullptr);

    ItemListModel* model() { return &m_model; }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_errorText; }
    QString title() const { return m_title; }

    // Movies, series and episodes matching `term`. An empty term clears the list.
    Q_INVOKABLE void search(const QString& term);
    Q_INVOKABLE void showSeasons(const QString& seriesId, const QString& seriesName);
    Q_INVOKABLE void showEpisodes(const QString& seriesId, const QString& seasonId,
                                  const QString& seasonName);
    // Opens a series or season, or asks to play anything else.
    Q_INVOKABLE void activate(int row);

  Q_SIGNALS:
    void loadingChanged();
    void errorTextChanged();
    void titleChanged();
    void playRequested(const QString& itemId);

  private:
    // Each listing gets a serial; an answer for an earlier serial is dropped,
    // so a slow search can never overwrite the list the user moved on to.
    quint64 beginRequest(const QString& title);
    void finish(quint64 serial, api::Result<api::ItemPage> result);
    void setLoading(bool loading);
    void setErrorText(const QString& text);

    api::JellyfinClient& m_client;
    ItemListModel m_model;
    quint64 m_serial = 0;
    bool m_loading = false;
    QString m_errorText;
    QString m_title;
  };

} // namespace jfd::library
