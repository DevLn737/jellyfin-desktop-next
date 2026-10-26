#pragma once

#include <jfd/library/CardImages.h>
#include <jfd/library/HomeSection.h>
#include <jfd/library/ItemListModel.h>

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>

#include <memory>
#include <vector>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  class LibraryUpdates;
  class RecommendationRows;

  // The home screen: the user's libraries, then rows of cards: "Continue
  // watching", "Next up", the latest items of each video library, the
  // recommendations, and the rows other modules add. Every row is a
  // section in `sections()`; the user's arrangement (order, rows switched
  // off) applies to all of them, and rows with nothing in them are left out.
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
    // For the "Customize home" screen: every section, in display order, as
    // maps with "id", "title" and "visible".
    Q_PROPERTY(QVariantList sectionList READ sectionList NOTIFY sectionListChanged)

  public:
    enum Role {
      TitleRole = Qt::UserRole + 1,
      // "resume", "nextUp", "latest", "recommend" or "section" (another
      // module's).
      KindRole,
      // The library a "latest" row belongs to.
      LibraryIdRole,
      ItemsRole,
      // "thumb" for wide cards, "poster" for tall ones, "square", "circle".
      CardShapeRole,
      // The section's id ("resume", "latest/<library>", "music.recent").
      SectionIdRole,
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
      // Rows of recommendations (RecommendationRows); off for a home screen
      // that only shows the server's own rows.
      bool recommendations = true;
      int recommendationRefreshMs = 5 * 60 * 1000;
      int recommendationTimeoutMs = 8000;
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
    // The row's cards when they are an ItemListModel (null otherwise).
    ItemListModel* rowItems(int row) const;
    QString rowKind(int row) const;

    // The sections the screen can show. Modules register theirs here
    // (`sections()->add(...)`); the built-in ones appear as the libraries
    // load.
    HomeSections* sections() { return &m_sections; }
    // The order and the rows switched off, as the user left them. Unknown
    // and new sections are tolerated: see HomeSections::displayOrder.
    HomeArrangement arrangement() const { return m_arrangement; }
    void setArrangement(const HomeArrangement& arrangement);
    QVariantList sectionList() const;
    // The section at `from` in sectionList() moves to `to`.
    Q_INVOKABLE void moveSection(int from, int to);
    Q_INVOKABLE void setSectionVisible(const QString& id, bool visible);
    Q_INVOKABLE void resetArrangement();
    // Forgets what another user had (the recommendations, the rows).
    void resetSession();

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
    void sectionListChanged();
    // The user changed the arrangement (for saving it).
    void arrangementChanged();

  private:
    struct Row {
      QString id;
      QString kind;
      QString title;
      QString libraryId;
      QString shape = QStringLiteral("poster");
      // Built-in rows own their cards; another module's rows point at
      // theirs.
      std::unique_ptr<ItemListModel> items;
      QPointer<QAbstractItemModel> model;
      std::function<void()> reload;
      QList<QMetaObject::Connection> connections;
      bool builtin = true;
      bool loaded = false;
    };

    Row* findRow(const QString& id) const;
    bool shown(const Row& row) const;
    void syncSections();
    void scheduleUpdate();
    void addBuiltinSection(const Row& row, int order);

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
    // The rows other modules registered as sections.
    std::vector<std::unique_ptr<Row>> m_external;
    // The rows with items, in order.
    QList<Row*> m_visible;
    HomeSections m_sections;
    HomeArrangement m_arrangement;
    RecommendationRows* m_recommendations = nullptr;
    bool m_syncing = false;
    bool m_updateQueued = false;
    QTimer m_reloadSoon;
    quint64 m_generation = 0;
    int m_pending = 0;
    QString m_errorText;
  };

} // namespace jfd::library
