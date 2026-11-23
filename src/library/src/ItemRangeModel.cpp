#include <jfd/library/ItemRangeModel.h>

#include <algorithm>

namespace jfd::library {

  ItemRangeModel::ItemRangeModel(QObject* parent) : QSortFilterProxyModel(parent) {
    for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
      connect(this, signal, this, &ItemRangeModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ItemRangeModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ItemRangeModel::countChanged);
    // Rows come in and out of the window as the source grows or shrinks:
    // the rows after an insertion or removal move, which the base class
    // does not filter again by itself.
    connect(this, &QAbstractProxyModel::sourceModelChanged, this, [this] {
      for (const QMetaObject::Connection& c : m_sourceConnections) disconnect(c);
      m_sourceConnections.clear();
      if (sourceModel() == nullptr) return;
      for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        m_sourceConnections.append(connect(sourceModel(), signal, this, &ItemRangeModel::refilter));
    });
  }

  void ItemRangeModel::setOffset(int offset) {
    offset = std::max(0, offset);
    if (offset == m_offset) return;
    m_offset = offset;
    refilter();
    Q_EMIT offsetChanged();
  }

  void ItemRangeModel::setLimit(int limit) {
    if (limit == m_limit) return;
    m_limit = limit;
    refilter();
    Q_EMIT limitChanged();
  }

  bool ItemRangeModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (sourceParent.isValid()) return false;
    return sourceRow >= m_offset && (m_limit < 0 || sourceRow - m_offset < m_limit);
  }

  void ItemRangeModel::refilter() {
    beginFilterChange();
    endFilterChange(Direction::Rows);
  }

} // namespace jfd::library
