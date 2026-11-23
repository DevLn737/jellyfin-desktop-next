#pragma once

#include <QList>
#include <QSortFilterProxyModel>

namespace jfd::library {

  // A window of rows of another model: rows offset .. offset + limit - 1, in
  // the source's order. A long season's page binds its episode grid to
  // one range of 24, so only those delegates and stills exist, whatever
  // the season's length; the full model still answers counts and
  // "first unwatched". A limit below 0 means no limit.
  class ItemRangeModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int offset READ offset WRITE setOffset NOTIFY offsetChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

  public:
    explicit ItemRangeModel(QObject* parent = nullptr);

    int offset() const { return m_offset; }
    void setOffset(int offset);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int count() const { return rowCount(); }

  Q_SIGNALS:
    void offsetChanged();
    void limitChanged();
    void countChanged();

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

  private:
    void refilter();

    QList<QMetaObject::Connection> m_sourceConnections;
    int m_offset = 0;
    int m_limit = -1;
  };

} // namespace jfd::library
