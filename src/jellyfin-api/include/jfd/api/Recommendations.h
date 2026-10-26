#pragma once

#include <jfd/api/Models.h>

#include <QJsonArray>
#include <QList>
#include <QString>

// What the server recommends: the categories of Movies/Recommendations.
namespace jfd::api {

  struct RecommendationGroup {
    // The server's RecommendationType: "SimilarToRecentlyPlayed",
    // "SimilarToLikedItem", "HasDirectorFromRecentlyPlayed",
    // "HasActorFromRecentlyPlayed", "HasLikedDirector", "HasLikedActor".
    // Kept as text so a type a newer server adds still comes through.
    QString type;
    // The film, director or actor the category is about.
    QString baselineName;
    // Stable for the same baseline; a UUID from the server.
    QString categoryId;
    QList<Item> items;
  };

  namespace json {
    // The answer of Movies/Recommendations: a bare JSON array. A category
    // without items is dropped.
    Result<QList<RecommendationGroup>> recommendationGroups(const QJsonArray& array);
  } // namespace json

} // namespace jfd::api
