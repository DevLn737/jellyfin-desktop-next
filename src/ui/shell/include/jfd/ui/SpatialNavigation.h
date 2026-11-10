#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <qqmlintegration.h>

// Arrow keys move the focus to the nearest focusable item in that direction,
// so a screen works with arrows, Enter and Back alone (a TV remote, a
// gamepad through InputRouter, a keyboard). Attached to the item whose
// subtree it serves, the shell:
//
//   AppShell { SpatialNavigation.enabled: true }
//
// Only keys nobody else took are used: the focused item, its Keys and
// KeyNavigation, a ListView or GridView moving between its own delegates
// and the pages' handlers come first. An arrow that reaches the attached
// item then moves the focus to the nearest visible, enabled item with
// activeFocusOnTab in that direction, measured in window coordinates, with
// the offset across the direction weighted (a card under the current one
// beats a nearer one off to the side). A focus scope with focusable items
// inside (a row of cards, a field) is entered, not focused as a whole.
// PageUp and PageDown move a page's height (gamepad shoulder buttons).
//
// When the focus lands inside a ListView or GridView, the view's
// currentIndex follows, so the view's own key handling continues from
// there; when it lands outside the visible part of a Flickable, the
// Flickable scrolls to it. Delegates a view has not created are reached
// through the view's own keys, not by geometry.
//
// A control that has to keep some arrows from its own key handling (a
// drop-down list, whose Up and Down would change the value) names them in a
// `navigationKeys` property (a list of Qt.Key values); those keys move the
// focus before the control sees them. Jfd.Ui.Controls cannot import this
// module, hence a property by convention instead of an attached one.
namespace jfd::ui {

  class SpatialNavigation : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SpatialNavigation is an attached property")
    QML_ATTACHED(SpatialNavigation)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

  public:
    enum Direction { Left, Right, Up, Down };
    Q_ENUM(Direction)

    explicit SpatialNavigation(QObject* parent = nullptr);
    ~SpatialNavigation() override;

    static SpatialNavigation* qmlAttachedProperties(QObject* object);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // Moves the window's focus from its focused item; false when there is
    // nothing in that direction (the focus stays).
    Q_INVOKABLE bool move(jfd::ui::SpatialNavigation::Direction direction);
    // About a page (the height of the Flickable around the focus) up or
    // down, by arrow steps; false when nothing around the focus scrolls or
    // the focus could not move.
    Q_INVOKABLE bool movePage(jfd::ui::SpatialNavigation::Direction direction);

    // The item `from` moves to among the focusable items under `scope`, or
    // nullptr. Pure geometry: no view's own key handling.
    static QQuickItem* findNext(QQuickItem* scope, QQuickItem* from, Direction direction);
    // Focuses `item` as a move would: the view around it follows
    // (currentIndex) and the Flickables around it scroll to show it.
    static void focusItem(QQuickItem* item);
    // Scrolls the Flickables around `item` until it is inside each of them.
    static void reveal(QQuickItem* item);

    // The arrow key's direction; false for other keys.
    static bool directionOfKey(int key, Direction* direction);

  Q_SIGNALS:
    void enabledChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QQuickItem* item() const;
    void attachToWindow();
    void onFocusChanged();
    bool handleKey(QKeyEvent* event, bool fromWindow);

    bool m_enabled = false;
    // The last input was a key (not a click or a touch).
    bool m_fromKeys = false;
    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_focusConnection;
  };

} // namespace jfd::ui
