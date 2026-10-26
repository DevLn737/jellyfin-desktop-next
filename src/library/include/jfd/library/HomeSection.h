#pragma once

#include <QAbstractItemModel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace jfd::library {

  // One row of the home screen. The built-in rows (Continue watching, Next
  // up, the latest of each library, recommendations) and the rows other
  // modules add ("Recently played", "Popular in Seerr") are all registered
  // as sections, and the user's order and choice of visible rows apply to
  // every one of them.
  //
  // This header has no Qt Quick in it, so jfd::music and jfd::seerr can
  // register sections without depending on the screens.
  struct HomeSection {
    // Stable and unique: "resume", "nextUp", "latest/<libraryId>",
    // "music.recent", "seerr.popular". The saved order refers to it.
    QString id;
    // Already translated.
    QString title;
    // The cards' shape: "poster", "thumb", "square" or "circle".
    QString cardShape = QStringLiteral("poster");
    // Place among the rows when the user has not arranged them; ascending.
    int defaultOrder = 0;
    // The cards, with the roles of ItemListModel (id, name, subtitle, image
    // URL, progress, ...). Owned by the module that registered the section;
    // the row shows only while it has items.
    QPointer<QAbstractItemModel> model;
    // Asks the module to refresh the model (returning to home); may be
    // empty.
    std::function<void()> reload;
  };

  // The sections the home screen can show. Sections come and go (a module
  // is switched off, a library is added), so ordering by the user's saved
  // list tolerates ids that are not registered and sections it has not seen.
  class HomeSections : public QObject {
    Q_OBJECT

  public:
    explicit HomeSections(QObject* parent = nullptr);

    // Adds a section, or replaces the one with the same id.
    void add(HomeSection section);
    void remove(const QString& id);
    bool contains(const QString& id) const;
    const HomeSection* find(const QString& id) const;
    // In default order (ties keep registration order).
    QList<const HomeSection*> byDefaultOrder() const;

    // The order the home screen shows: the ids of `arrangement` that are
    // registered (in that order, once each), then the registered sections
    // it does not mention, in default order. Unknown ids are ignored.
    QStringList displayOrder(const QStringList& arrangement) const;

  Q_SIGNALS:
    void changed();

  private:
    QList<HomeSection> m_sections;
  };

} // namespace jfd::library
