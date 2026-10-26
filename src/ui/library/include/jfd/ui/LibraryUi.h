#pragma once

#include <jfd/library/HomeModel.h>
#include <jfd/library/ItemDetails.h>
#include <jfd/library/LibraryModel.h>
#include <jfd/library/SearchModel.h>
#include <jfd/library/SignIn.h>

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <qqmlintegration.h>

#include <functional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {
  class CollectionIndex;
  class LibraryUpdates;
}

namespace jfd::ui {

  // The library screens' way into the library module (QML: `Library`): the
  // sign-in flow, the home page, search, models for a library grid or an
  // item page, image URLs for the image provider, and Play / Watch together.
  //
  // The engine creates the singleton; the application attaches the
  // services before the window loads. Play and Watch together go to
  // handlers the playback and group modules set; until one is set the
  // button says that it is not available yet.
  class LibraryUi : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Library)
    QML_SINGLETON
    Q_PROPERTY(jfd::library::SignIn* signIn READ signIn NOTIFY attached)
    Q_PROPERTY(jfd::library::HomeModel* home READ home NOTIFY attached)
    Q_PROPERTY(jfd::library::SearchModel* search READ search NOTIFY attached)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY handlersChanged)
    Q_PROPERTY(bool canWatchTogether READ canWatchTogether NOTIFY handlersChanged)

  public:
    struct Services {
      api::JellyfinClient* client = nullptr;
      library::SignIn* signIn = nullptr;
      library::HomeModel* home = nullptr;
      library::SearchModel* search = nullptr;
      library::LibraryUpdates* updates = nullptr;
      library::CollectionIndex* collections = nullptr;
      qreal devicePixelRatio = 1.0;
      // Each library's sort and filters, keyed by library id.
      std::function<QJsonObject()> readFilters;
      std::function<void(const QJsonObject&)> writeFilters;
    };

    using PlayHandler = std::function<void(const QString& itemId, qint64 startPositionTicks)>;
    using WatchTogetherHandler =
        std::function<void(const QString& itemId, const QString& name, qint64 startPositionTicks)>;

    explicit LibraryUi(QObject* parent = nullptr);

    void attach(const Services& services);
    void setPlayHandler(PlayHandler handler);
    void setWatchTogetherHandler(WatchTogetherHandler handler);

    library::SignIn* signIn() const { return m_services.signIn; }
    library::HomeModel* home() const { return m_services.home; }
    library::SearchModel* search() const { return m_services.search; }
    bool canPlay() const { return bool(m_play); }
    bool canWatchTogether() const { return bool(m_watchTogether); }

    // A grid for one library, owned by `owner` (the page), with the sort
    // and filters it had last time; changes to them are kept. Null before
    // attach.
    Q_INVOKABLE jfd::library::LibraryModel* createLibrary(const QString& libraryId, QObject* owner);
    // A grid of one collection's items, owned by `owner`. Null before attach.
    Q_INVOKABLE jfd::library::LibraryModel* createCollection(const QString& collectionId,
                                                             QObject* owner);
    // An item page's model, owned by `owner`. Null before attach.
    Q_INVOKABLE jfd::library::ItemDetails* createDetails(const QString& itemId, QObject* owner);
    // A search of its own (the command palette's), owned by `owner`; the
    // search page keeps using `search`. Null before attach.
    Q_INVOKABLE jfd::library::SearchModel* createSearch(QObject* owner);
    // The image provider's source for a server image URL.
    Q_INVOKABLE QString image(const QUrl& url) const;
    // Where a card leads: "library/<id>" for a library, "collection/<id>"
    // for a collection, "item/<id>" for anything else.
    Q_INVOKABLE QString routeFor(const QString& itemId, const QString& type) const;
    // A library's title from the home page's list, for the grid's heading.
    Q_INVOKABLE QString libraryName(const QString& libraryId) const;
    // "1 h 52 min", "45 min".
    Q_INVOKABLE QString formatRunTime(qint64 ticks) const;

    Q_INVOKABLE void play(const QString& itemId, qint64 startPositionTicks);
    Q_INVOKABLE void watchTogether(const QString& itemId, const QString& name,
                                   qint64 startPositionTicks);

  Q_SIGNALS:
    void attached();
    void handlersChanged();
    // For the shell's toast.
    void message(const QString& text, bool error);

  private:
    QString collectionTypeOf(const QString& libraryId) const;
    library::LibraryModel* newModel(QObject* owner) const;

    // Filters of the libraries opened this session, when there is no store.
    QJsonObject m_filters;

    Services m_services;
    PlayHandler m_play;
    WatchTogetherHandler m_watchTogether;
  };

} // namespace jfd::ui
