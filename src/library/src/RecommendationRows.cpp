#include <jfd/library/RecommendationRows.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/CardImages.h>

#include <QCoreApplication>
#include <QLoggingCategory>

#include <algorithm>
#include <functional>

namespace jfd::library {

  namespace {
    Q_LOGGING_CATEGORY(lcRecommendations, "jfd.library.recommendations")

    constexpr QLatin1String MoviePrefix{"recommend/movie/"};
    constexpr QLatin1String SeriesPrefix{"recommend/series/"};
    constexpr QLatin1String PicksId{"recommend/picks"};

    // Between the built-in rows (Continue watching ... Latest) and the other
    // modules' rows.
    constexpr int MovieOrder = 210;
    constexpr int SeriesOrder = 230;
    constexpr int PicksOrder = 250;

    QString tr(const char* text) {
      return QCoreApplication::translate("Home", text);
    }

    QString movieTitle(const api::RecommendationGroup& group) {
      const QString name = group.baselineName;
      if (name.isEmpty()) return tr("Recommended for you");
      if (group.type == QLatin1String("SimilarToRecentlyPlayed")) return tr("Because you watched %1").arg(name);
      if (group.type == QLatin1String("SimilarToLikedItem")) return tr("Because you liked %1").arg(name);
      if (group.type == QLatin1String("HasDirectorFromRecentlyPlayed") || group.type == QLatin1String("HasLikedDirector"))
        return tr("Directed by %1").arg(name);
      if (group.type == QLatin1String("HasActorFromRecentlyPlayed") || group.type == QLatin1String("HasLikedActor"))
        return tr("Starring %1").arg(name);
      return name;
    }
  } // namespace

  // One refresh: three independent parts, each publishing when it is done.
  struct RecommendationRows::Task {
    quint64 generation = 0;
    int unfinished = 3;
    // The series part collects rows from several answers.
    int seriesPending = 0;
    QList<Row> seriesRows;
  };

  RecommendationRows::RecommendationRows(api::JellyfinClient& client, HomeSections& sections, Options options,
                                         QObject* parent)
      : QObject(parent), m_client(client), m_sections(sections), m_options(options) {
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(m_options.timeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
      // What has not come is left out; the answers still to arrive are
      // dropped (they belong to an older generation).
      qCInfo(lcRecommendations) << "recommendations timed out";
      ++m_generation;
      m_running.clear();
    });
  }

  void RecommendationRows::refresh(bool force) {
    if (!force && m_hasRefreshed && m_lastRefresh.isValid() &&
        m_lastRefresh.elapsed() < m_options.refreshMs) {
      return;
    }
    m_hasRefreshed = true;
    m_lastRefresh.start();
    const quint64 generation = ++m_generation;
    m_running.clear();
    auto task = std::make_shared<Task>();
    task->generation = generation;
    m_running.append(task);
    m_timeout.start();
    startMovies(task);
    startSeries(task);
    startPicks(task);
  }

  void RecommendationRows::clear() {
    ++m_generation;
    m_running.clear();
    m_timeout.stop();
    m_hasRefreshed = false;
    for (const QString& id : std::as_const(m_registered)) m_sections.remove(id);
    m_registered.clear();
    m_models.clear();
  }

  void RecommendationRows::finish(const std::shared_ptr<Task>& task) {
    if (--task->unfinished == 0) {
      m_timeout.stop();
      m_running.removeAll(task);
    }
  }

  void RecommendationRows::publish(const QString& prefix, const QList<Row>& rows) {
    const QSize pixels = cardImageSize(CardShape::Poster, m_options.cardWidth, m_options.devicePixelRatio);
    QSet<QString> now;
    for (const Row& row : rows) {
      if (row.items.isEmpty()) continue;
      now.insert(row.id);
      auto& model = m_models[row.id];
      if (!model) model = std::make_unique<ItemListModel>();
      QList<ItemListModel::Entry> entries;
      for (const api::Item& item : row.items) {
        entries.append({item, cardImageUrl(m_client, item, CardShape::Poster, pixels)});
      }
      model->setEntries(std::move(entries));
      HomeSection section;
      section.id = row.id;
      section.title = row.title;
      section.cardShape = QStringLiteral("poster");
      section.defaultOrder = row.order;
      section.model = model.get();
      m_sections.add(std::move(section));
      m_registered.insert(row.id);
    }
    // Rows of this kind that are gone leave the screen before their models do.
    const QSet<QString> before = m_registered;
    for (const QString& id : before) {
      const bool ours = prefix == PicksId ? id == PicksId : id.startsWith(prefix);
      if (ours && !now.contains(id)) {
        m_sections.remove(id);
        m_registered.remove(id);
        m_models.erase(id);
      }
    }
  }

  void RecommendationRows::startMovies(const std::shared_ptr<Task>& task) {
    m_client.movieRecommendations(
        m_options.movieCategories, m_options.itemsPerRow, this,
        [this, task](api::Result<QList<api::RecommendationGroup>> result) {
          if (task->generation != m_generation) return;
          QList<Row> rows;
          if (result) {
            int n = 0;
            for (const api::RecommendationGroup& group : *result) {
              Row row;
              row.id = MoviePrefix + (group.categoryId.isEmpty() ? group.type + QLatin1Char('/') + group.baselineName
                                                                  : group.categoryId);
              row.title = movieTitle(group);
              row.order = MovieOrder + n++;
              row.items = group.items;
              rows.append(std::move(row));
            }
          } else {
            // A server without the endpoint, or no history: no rows.
            qCInfo(lcRecommendations) << "movie recommendations unavailable:" << result.error().message;
          }
          publish(MoviePrefix, rows);
          finish(task);
        });
  }

  void RecommendationRows::startSeries(const std::shared_ptr<Task>& task) {
    api::ItemQuery query;
    query.includeItemTypes = {QStringLiteral("Episode")};
    query.isPlayed = true;
    query.sortBy = QStringLiteral("DatePlayed");
    query.descending = true;
    query.limit = 40;
    m_client.items(query, this, [this, task](api::Result<api::ItemPage> result) {
      if (task->generation != m_generation) return;
      QList<QPair<QString, QString>> series;
      if (result) {
        for (const api::Item& episode : result->items) {
          if (episode.seriesId.isEmpty()) continue;
          const bool seen = std::any_of(series.begin(), series.end(),
                                        [&](const auto& s) { return s.first == episode.seriesId; });
          if (!seen) series.append({episode.seriesId, episode.seriesName});
          if (series.size() >= m_options.seriesRows) break;
        }
      }
      if (series.isEmpty()) {
        publish(SeriesPrefix, {});
        finish(task);
        return;
      }
      task->seriesPending = static_cast<int>(series.size());
      for (int i = 0; i < series.size(); ++i) {
        const QString id = series[i].first;
        const QString name = series[i].second;
        m_client.similarItems(id, m_options.itemsPerRow, this,
                              [this, task, id, name, i](api::Result<api::ItemPage> similar) {
                                if (task->generation != m_generation) return;
                                if (similar) {
                                  Row row;
                                  row.id = SeriesPrefix + id;
                                  row.title = name.isEmpty() ? tr("Because you watched this")
                                                             : tr("Because you watched %1").arg(name);
                                  row.order = SeriesOrder + i;
                                  row.items = similar->items;
                                  task->seriesRows.append(std::move(row));
                                }
                                if (--task->seriesPending == 0) {
                                  std::sort(task->seriesRows.begin(), task->seriesRows.end(),
                                            [](const Row& a, const Row& b) { return a.order < b.order; });
                                  publish(SeriesPrefix, task->seriesRows);
                                  finish(task);
                                }
                              });
      }
    });
  }

  void RecommendationRows::startPicks(const std::shared_ptr<Task>& task) {
    m_client.suggestions(m_options.picks, {QStringLiteral("Movie"), QStringLiteral("Series")}, this,
                         [this, task](api::Result<api::ItemPage> result) {
                           if (task->generation != m_generation) return;
                           QList<Row> rows;
                           if (result && !result->items.isEmpty()) {
                             Row row;
                             row.id = PicksId;
                             row.title = tr("Picks");
                             row.order = PicksOrder;
                             row.items = result->items;
                             rows.append(std::move(row));
                           } else if (!result) {
                             qCInfo(lcRecommendations) << "suggestions unavailable:" << result.error().message;
                           }
                           publish(PicksId, rows);
                           finish(task);
                         });
  }

} // namespace jfd::library
