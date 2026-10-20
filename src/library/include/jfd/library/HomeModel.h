#pragma once

#include <jfd/library/CardImages.h>
#include <jfd/library/ItemListModel.h>

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  class LibraryUpdates;

  // The home screen: the user's libraries, then rows of cards in the order
  // Jellyfin Web shows them: "Continue watching", "Next up", and the latest
  // items of each video library. Rows with nothing in them are left out.
  //
  // A reload keeps the rows on screen and replaces their items when the
  // answers arrive, so returning home never shows an empty page; answers to
  // an older reload are dropped.
  class HomeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(jfd::library::ItemListModel* libraries READ libraries CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

  public:
    enum Role {
      TitleRole = Qt::UserRole + 1,
      // "resume", "nextUp" or "latest".
      KindRole,
      // The library a "latest" row belongs to.
      LibraryIdRole,
      ItemsRole,
      // "thumb" for wide cards, "poster" for tall ones.
      CardShapeRole,
    };
    Q_ENUM(Role)

    struct Options {
      int resumeLimit = 12;
      int nextUpLimit = 24;
      int latestLimit = 16;
      // Card width in device-independent pixels and the screen's ratio, for
      // the image sizes.
      int cardWidth = 200;
      qreal devicePixelRatio = 1.0;
      // Server updates are collected for this long before a reload.
      int updateDelayMs = 3000;
    };

    HomeModel(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit HomeModel(api::JellyfinClient& client, QObject* parent = nullptr)
        : HomeModel(client, Options{}, parent) {}
    ~HomeModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    ItemListModel* libraries() { return &m_libraries; }
    bool loading() const { return m_pending > 0; }
    QString errorText() const { return m_errorText; }
    int count() const { return static_cast<int>(m_visible.size()); }
    ItemListModel* rowItems(int row) const;
    QString rowKind(int row) const;

    // Keeps rows current with watched state and favorites, and reloads on
    // library changes and reconnects.
    void follow(LibraryUpdates& updates);

    Q_INVOKABLE void reload();
    // Reloads after the update delay, once however often it is called.
    void reloadSoon();

  Q_SIGNALS:
    void loadingChanged();
    void errorTextChanged();
    void countChanged();

  private:
    struct Row {
      QString kind;
      QString title;
      QString libraryId;
      CardShape shape = CardShape::Poster;
      std::unique_ptr<ItemListModel> items;
      bool loaded = false;
    };

    void fill(quint64 generation, Row* row, api::Result<api::ItemPage> result);
    void loadRows(quint64 generation);
    void updateVisible();
    void requestDone();
    void setError(const QString& text);
    QList<ItemListModel::Entry> entries(const QList<api::Item>& items, CardShape shape) const;

    api::JellyfinClient& m_client;
    Options m_options;
    ItemListModel m_libraries;
    // Every row in display order, shown or not.
    std::vector<std::unique_ptr<Row>> m_rows;
    // The rows with items, in order.
    QList<Row*> m_visible;
    QTimer m_reloadSoon;
    quint64 m_generation = 0;
    int m_pending = 0;
    QString m_errorText;
  };

} // namespace jfd::library
