#include "jfd/ui/LibraryUi.h"

#include <jfd/library/CollectionIndex.h>
#include <jfd/library/ItemListModel.h>
#include <jfd/library/LibraryUpdates.h>
#include <jfd/ui/LibraryImageProvider.h>

#include <QLoggingCategory>
#include <QQmlEngine>

namespace jfd::ui {

  namespace {
    Q_LOGGING_CATEGORY(lcLibraryUi, "jfd.ui.library")

    constexpr qint64 TicksPerMinute = 600'000'000;
  } // namespace

  LibraryUi::LibraryUi(QObject* parent) : QObject(parent) {}

  void LibraryUi::attach(const Services& services) {
    m_services = services;
    Q_EMIT attached();
  }

  void LibraryUi::setPlayHandler(PlayHandler handler) {
    m_play = std::move(handler);
    Q_EMIT handlersChanged();
  }

  void LibraryUi::setWatchTogetherHandler(WatchTogetherHandler handler) {
    m_watchTogether = std::move(handler);
    Q_EMIT handlersChanged();
  }

  library::LibraryModel* LibraryUi::newModel(QObject* owner) const {
    library::LibraryModel::Options options;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* model = new library::LibraryModel(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    if (m_services.updates != nullptr) model->follow(*m_services.updates);
    return model;
  }

  library::SearchModel* LibraryUi::createSearch(QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::SearchModel::Options options;
    // The palette shows a few results and answers quickly.
    options.delayMs = 200;
    options.limit = 12;
    options.cardWidth = 80;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* model = new library::SearchModel(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    if (m_services.updates != nullptr) model->follow(*m_services.updates);
    return model;
  }

  library::LibraryModel* LibraryUi::createLibrary(const QString& libraryId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LibraryModel* model = newModel(owner);
    // The collection type decides the item types, so it goes first.
    model->setCollectionType(collectionTypeOf(libraryId));
    model->setLibraryId(libraryId);
    const QJsonObject saved = m_services.readFilters ? m_services.readFilters() : m_filters;
    model->applyFilters(saved.value(libraryId).toObject());
    connect(model, &library::LibraryModel::queryChanged, this, [this, model, libraryId] {
      QJsonObject all = m_services.readFilters ? m_services.readFilters() : m_filters;
      const QJsonObject filters = model->filters();
      if (all.value(libraryId).toObject() == filters) return;
      all.insert(libraryId, filters);
      if (m_services.writeFilters)
        m_services.writeFilters(all);
      else
        m_filters = all;
    });
    return model;
  }

  library::LibraryModel* LibraryUi::createCollection(const QString& collectionId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::LibraryModel* model = newModel(owner);
    model->setCollectionType(library::LibraryModel::CollectionItems);
    // By release date, oldest first: a film series in its order.
    model->setSortBy(QStringLiteral("PremiereDate"));
    model->setLibraryId(collectionId);
    return model;
  }

  library::ItemDetails* LibraryUi::createDetails(const QString& itemId, QObject* owner) {
    if (m_services.client == nullptr) return nullptr;
    library::ItemDetails::Options options;
    options.devicePixelRatio = m_services.devicePixelRatio;
    auto* details = new library::ItemDetails(*m_services.client, options, owner);
    QQmlEngine::setObjectOwnership(details, QQmlEngine::CppOwnership);
    details->setUpdates(m_services.updates);
    details->setCollectionIndex(m_services.collections);
    connect(details, &library::ItemDetails::playRequested, this,
            [this, details](const QString& id, qint64 startTicks, bool together) {
              if (together) {
                watchTogether(id, details->name(), startTicks);
              } else {
                play(id, startTicks);
              }
            });
    details->setItemId(itemId);
    return details;
  }

  QString LibraryUi::image(const QUrl& url) const {
    return LibraryImageProvider::sourceFor(url);
  }

  QString LibraryUi::routeFor(const QString& itemId, const QString& type) const {
    if (type == QLatin1String("BoxSet")) return QStringLiteral("collection/") + itemId;
    if (type == QLatin1String("CollectionFolder") || type == QLatin1String("UserView") ||
        type == QLatin1String("Folder")) {
      return QStringLiteral("library/") + itemId;
    }
    // A Seerr title (id like "movie:603"), added to the home screen by the
    // Seerr module.
    if (type == QLatin1String("SeerrTitle")) return QStringLiteral("seerr-title/") + itemId;
    return QStringLiteral("item/") + itemId;
  }

  QString LibraryUi::libraryName(const QString& libraryId) const {
    if (m_services.home == nullptr) return {};
    const library::ItemListModel* libraries = m_services.home->libraries();
    const int row = libraries->rowOf(libraryId);
    return row < 0 ? QString() : libraries->entry(row)->item.name;
  }

  QString LibraryUi::collectionTypeOf(const QString& libraryId) const {
    if (m_services.home == nullptr) return {};
    const library::ItemListModel* libraries = m_services.home->libraries();
    const int row = libraries->rowOf(libraryId);
    return row < 0 ? QString() : libraries->entry(row)->item.collectionType;
  }

  QString LibraryUi::formatRunTime(qint64 ticks) const {
    const qint64 minutes = (ticks + TicksPerMinute / 2) / TicksPerMinute;
    if (minutes <= 0) return {};
    if (minutes < 60) return tr("%n min", nullptr, int(minutes));
    const qint64 hours = minutes / 60;
    const qint64 rest = minutes % 60;
    return rest == 0 ? tr("%n h", nullptr, int(hours)) : tr("%1 h %2 min").arg(hours).arg(rest);
  }

  void LibraryUi::play(const QString& itemId, qint64 startPositionTicks) {
    if (itemId.isEmpty()) return;
    if (!m_play) {
      qCInfo(lcLibraryUi) << "Play" << itemId << "at" << startPositionTicks
                          << "with no player installed";
      Q_EMIT message(tr("Playback is not available in this build yet."), false);
      return;
    }
    m_play(itemId, startPositionTicks);
  }

  void LibraryUi::watchTogether(const QString& itemId, const QString& name,
                                qint64 startPositionTicks) {
    if (itemId.isEmpty()) return;
    if (!m_watchTogether) {
      qCInfo(lcLibraryUi) << "Watch together" << itemId << "with no group module installed";
      Q_EMIT message(tr("Watching together is not available in this build yet."), false);
      return;
    }
    m_watchTogether(itemId, name, startPositionTicks);
  }

} // namespace jfd::ui
