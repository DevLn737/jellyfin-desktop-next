#include "jfd/library/HomeSection.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace jfd::library {

  HomeSections::HomeSections(QObject* parent) : QObject(parent) {}

  void HomeSections::add(HomeSection section) {
    if (section.id.isEmpty()) {
      return;
    }
    const auto same = [&](const HomeSection& other) { return other.id == section.id; };
    if (const auto existing = std::find_if(m_sections.begin(), m_sections.end(), same);
        existing != m_sections.end()) {
      *existing = std::move(section);
    } else {
      m_sections.append(std::move(section));
    }
    Q_EMIT changed();
  }

  void HomeSections::remove(const QString& id) {
    if (m_sections.removeIf([&](const HomeSection& section) { return section.id == id; }) > 0) {
      Q_EMIT changed();
    }
  }

  bool HomeSections::contains(const QString& id) const { return find(id) != nullptr; }

  const HomeSection* HomeSections::find(const QString& id) const {
    for (const HomeSection& section : m_sections) {
      if (section.id == id) {
        return &section;
      }
    }
    return nullptr;
  }

  QList<const HomeSection*> HomeSections::byDefaultOrder() const {
    QList<const HomeSection*> sorted;
    for (const HomeSection& section : m_sections) {
      sorted.append(&section);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const HomeSection* a, const HomeSection* b) { return a->defaultOrder < b->defaultOrder; });
    return sorted;
  }

  QStringList HomeSections::displayOrder(const QStringList& arrangement) const {
    QStringList order;
    for (const QString& id : arrangement) {
      if (contains(id) && !order.contains(id)) {
        order.append(id);
      }
    }
    for (const HomeSection* section : byDefaultOrder()) {
      if (!order.contains(section->id)) {
        order.append(section->id);
      }
    }
    return order;
  }

  HomeArrangement parseArrangement(const QString& stored) {
    HomeArrangement arrangement;
    const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8());
    if (!document.isArray()) {
      return arrangement;
    }
    for (const QJsonValue& value : document.array()) {
      QString id = value.toString();
      const bool hidden = id.startsWith(QLatin1Char('-'));
      if (hidden) {
        id.remove(0, 1);
      }
      if (id.isEmpty() || arrangement.order.contains(id)) {
        continue;
      }
      arrangement.order.append(id);
      if (hidden) {
        arrangement.hidden.append(id);
      }
    }
    return arrangement;
  }

  QString formatArrangement(const HomeArrangement& arrangement) {
    QJsonArray array;
    for (const QString& id : arrangement.order) {
      array.append(arrangement.hidden.contains(id) ? QLatin1Char('-') + id : id);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
  }

} // namespace jfd::library
