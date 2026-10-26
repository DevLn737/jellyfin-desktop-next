#include <jfd/api/Recommendations.h>

namespace jfd::api::json {

  Result<QList<RecommendationGroup>> recommendationGroups(const QJsonArray& array) {
    QList<RecommendationGroup> groups;
    for (const QJsonValue& value : array) {
      const QJsonObject object = value.toObject();
      RecommendationGroup group;
      group.type = object.value(QLatin1String("RecommendationType")).toString();
      group.baselineName = object.value(QLatin1String("BaselineItemName")).toString();
      group.categoryId = object.value(QLatin1String("CategoryId")).toString();
      const QJsonValue items = object.value(QLatin1String("Items"));
      if (!items.isArray()) continue;
      for (const QJsonValue& entry : items.toArray()) {
        auto parsed = item(entry.toObject());
        if (!parsed) return parsed.error();
        group.items.append(std::move(parsed).value());
      }
      if (!group.items.isEmpty()) groups.append(std::move(group));
    }
    return groups;
  }

} // namespace jfd::api::json
