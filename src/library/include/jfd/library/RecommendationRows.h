#pragma once

#include <jfd/library/HomeSection.h>
#include <jfd/library/ItemListModel.h>

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <map>
#include <memory>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  // The home screen's recommendation rows, registered as sections:
  //  - "recommend/movie/<category>": the server's Movies/Recommendations
  //    ("Because you watched ...", "Directed by ...");
  //  - "recommend/series/<series>": "Because you watched <series>" from
  //    the series' similar items, for the last few series the user watched
  //    (done here, the server has no such list);
  //  - "recommend/picks": "Picks", the server's Items/Suggestions.
  //
  // All of it is best effort: a request that fails or is slow leaves its
  // rows out (never an error on the home screen), sections that end up empty
  // are not shown, and the answers are kept for the session and fetched
  // again at most every few minutes.
  class RecommendationRows : public QObject {
    Q_OBJECT

  public:
    struct Options {
      int movieCategories = 4;
      int itemsPerRow = 16;
      // How many of the last watched series give a row.
      int seriesRows = 3;
      int picks = 16;
      // Fetched again no sooner than this after the last time.
      int refreshMs = 5 * 60 * 1000;
      // What has not arrived by then is left out.
      int timeoutMs = 8000;
      int cardWidth = 200;
      qreal devicePixelRatio = 1.0;
    };

    RecommendationRows(api::JellyfinClient& client, HomeSections& sections, Options options,
                       QObject* parent = nullptr);

    // Fetches the rows unless they are fresh (or `force`).
    void refresh(bool force = false);
    // Forgets everything (another user signed in).
    void clear();

    // The sections registered now, in default order, for tests.
    QStringList sectionIds() const { return m_registered.values(); }

  private:
    struct Row {
      QString id;
      QString title;
      int order = 0;
      QList<api::Item> items;
    };
    struct Task;

    void publish(const QString& prefix, const QList<Row>& rows);
    void startMovies(const std::shared_ptr<Task>& task);
    void startSeries(const std::shared_ptr<Task>& task);
    void startPicks(const std::shared_ptr<Task>& task);
    void finish(const std::shared_ptr<Task>& task);

    api::JellyfinClient& m_client;
    HomeSections& m_sections;
    Options m_options;
    std::map<QString, std::unique_ptr<ItemListModel>> m_models;
    QSet<QString> m_registered;
    QElapsedTimer m_lastRefresh;
    bool m_hasRefreshed = false;
    quint64 m_generation = 0;
    QTimer m_timeout;
    QList<std::shared_ptr<Task>> m_running;
  };

} // namespace jfd::library
