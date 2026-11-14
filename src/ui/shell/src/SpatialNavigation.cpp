#include "SpatialNavigation.h"

#include <QCoreApplication>
#include <QJSValue>
#include <QKeyEvent>
#include <QMetaEnum>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace jfd::ui {

  namespace {
    // Across the direction, a gap counts this many times more than a gap
    // along it; the distance between centres only breaks ties.
    constexpr double CrossWeight = 3.0;
    constexpr double CentreWeight = 0.25;
    // The room left around an item scrolled into view.
    constexpr double RevealMargin = 16.0;
    // A page move ends after this share of the page.
    constexpr double PageShare = 0.9;
    constexpr int MaxPageSteps = 200;

    bool isFlickable(const QQuickItem* item) { return item->inherits("QQuickFlickable"); }
    bool isItemView(const QQuickItem* item) { return item->inherits("QQuickItemView"); }
    bool isFocusScope(const QQuickItem* item) { return item->flags().testFlag(QQuickItem::ItemIsFocusScope); }

    QRectF sceneRect(const QQuickItem* item) {
      return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    }

    QQuickItem* contentItemOf(const QQuickItem* flickable) {
      return qvariant_cast<QQuickItem*>(flickable->property("contentItem"));
    }

    struct Candidate {
      QQuickItem* item = nullptr;
      QRectF rect;
    };

    // The focusable items under `node`, deepest first. A focus scope with
    // focusable items inside is left out: those are the candidates. Items
    // must be on the window, except inside a Flickable, which scrolls them
    // into view; but in a ListView or GridView only the delegates in its
    // visible part count (`viewClip`), the rest it reaches by its own keys
    // (a row of cards keeps cached cards beside its edges). Returns whether
    // the subtree had any.
    bool collect(QQuickItem* node, bool insideFlickable, const std::optional<QRectF>& viewClip,
                 const QRectF& window, std::vector<Candidate>& out) {
      if (!node->isVisible() || !node->isEnabled() || node->opacity() <= 0.0) return false;
      const bool childrenInsideFlickable = insideFlickable || isFlickable(node);
      std::optional<QRectF> childrenClip = viewClip;
      if (isItemView(node)) childrenClip = viewClip ? viewClip->intersected(sceneRect(node)) : sceneRect(node);
      bool below = false;
      const QList<QQuickItem*> children = node->childItems();
      for (QQuickItem* child : children)
        below = collect(child, childrenInsideFlickable, childrenClip, window, out) || below;
      if (!node->activeFocusOnTab() || node->width() <= 0 || node->height() <= 0) return below;
      if (below && isFocusScope(node)) return true;
      const QRectF rect = sceneRect(node);
      if (viewClip && !rect.intersects(*viewClip)) return below;
      if (!insideFlickable && !rect.intersects(window)) return below;
      out.push_back({node, rect});
      return true;
    }

    // How far `to` is from `from` in the direction; negative when it is not
    // in that direction at all.
    double score(const QRectF& from, const QRectF& to, SpatialNavigation::Direction direction) {
      const QPointF a = from.center();
      const QPointF b = to.center();
      double along = 0;
      double crossGap = 0;
      double centres = 0;
      switch (direction) {
      case SpatialNavigation::Right:
        if (b.x() <= a.x() + 0.5) return -1;
        along = to.left() - from.right();
        break;
      case SpatialNavigation::Left:
        if (b.x() >= a.x() - 0.5) return -1;
        along = from.left() - to.right();
        break;
      case SpatialNavigation::Down:
        if (b.y() <= a.y() + 0.5) return -1;
        along = to.top() - from.bottom();
        break;
      case SpatialNavigation::Up:
        if (b.y() >= a.y() - 0.5) return -1;
        along = from.top() - to.bottom();
        break;
      }
      if (direction == SpatialNavigation::Left || direction == SpatialNavigation::Right) {
        crossGap = std::max({0.0, to.top() - from.bottom(), from.top() - to.bottom()});
        centres = std::abs(b.y() - a.y());
      } else {
        crossGap = std::max({0.0, to.left() - from.right(), from.left() - to.right()});
        centres = std::abs(b.x() - a.x());
      }
      return std::max(0.0, along) + CrossWeight * crossGap + CentreWeight * centres;
    }

    // The index of the ListView or GridView delegate `item` is in, or -1:
    // the delegate's `index` (a required property or its context's), else
    // the view's item at the delegate's centre.
    int delegateIndex(QQuickItem* view, QQuickItem* item) {
      QQuickItem* content = contentItemOf(view);
      if (!content) return -1;
      QQuickItem* delegate = item;
      while (delegate && delegate->parentItem() != content) delegate = delegate->parentItem();
      if (!delegate) return -1;
      bool ok = false;
      int index = delegate->property("index").toInt(&ok);
      if (ok) return index;
      if (const QQmlContext* context = qmlContext(delegate)) {
        index = context->contextProperty(QStringLiteral("index")).toInt(&ok);
        if (ok) return index;
      }
      const QPointF centre = delegate->mapToItem(content, QPointF(delegate->width() / 2, delegate->height() / 2));
      index = -1;
      QMetaObject::invokeMethod(view, "indexAt", Q_RETURN_ARG(int, index), Q_ARG(qreal, centre.x()),
                                Q_ARG(qreal, centre.y()));
      return index;
    }

    bool fullyInside(const QQuickItem* item, const QQuickItem* viewport) {
      const QRectF rect = item->mapRectToItem(viewport, QRectF(0, 0, item->width(), item->height()));
      return rect.top() >= -0.5 && rect.left() >= -0.5 && rect.bottom() <= viewport->height() + 0.5 &&
             rect.right() <= viewport->width() + 0.5;
    }

    // The view scrolls just enough to show its item at `index`.
    void showIndex(QQuickItem* view, int index) {
      const QMetaObject* meta = view->metaObject();
      const int enumIndex = meta->indexOfEnumerator("PositionMode");
      const int contain = enumIndex >= 0 ? meta->enumerator(enumIndex).keyToValue("Contain") : -1;
      if (contain >= 0) QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, contain));
    }

    // The views around `item` make its delegate their current one, so their
    // own keys go on from there, and show it.
    void followInViews(QQuickItem* item) {
      for (QQuickItem* node = item->parentItem(); node; node = node->parentItem()) {
        if (!isItemView(node)) continue;
        // The view moved there itself.
        const auto* current = qvariant_cast<QQuickItem*>(node->property("currentItem"));
        if (current && (current == item || current->isAncestorOf(item))) continue;
        const int index = delegateIndex(node, item);
        if (index < 0 || node->property("currentIndex").toInt() == index) continue;
        node->setProperty("currentIndex", index);
        if (!fullyInside(item, node)) showIndex(node, index);
      }
    }

    // A step through the innermost ListView or GridView around `item` that
    // moves that way and has its own key navigation: its currentIndex
    // advances as by its arrow key, the view shows the new current item,
    // and the focus goes to it. Nothing is sent to the focused item, so an
    // editor's value never changes. False when no view can step (at its
    // end, or none around).
    bool stepInView(QQuickItem* item, SpatialNavigation::Direction direction) {
      const bool vertical = direction == SpatialNavigation::Up || direction == SpatialNavigation::Down;
      const bool forward = direction == SpatialNavigation::Down || direction == SpatialNavigation::Right;
      for (QQuickItem* view = item->parentItem(); view; view = view->parentItem()) {
        if (!isItemView(view) || !view->property("keyNavigationEnabled").toBool()) continue;
        const char* method = nullptr;
        if (view->inherits("QQuickGridView")) {
          method = direction == SpatialNavigation::Up     ? "moveCurrentIndexUp"
                   : direction == SpatialNavigation::Down ? "moveCurrentIndexDown"
                   : direction == SpatialNavigation::Left ? "moveCurrentIndexLeft"
                                                          : "moveCurrentIndexRight";
        } else if (view->inherits("QQuickListView")) {
          const bool listVertical = view->property("orientation").toInt() == Qt::Vertical;
          if (listVertical != vertical) continue;
          method = forward ? "incrementCurrentIndex" : "decrementCurrentIndex";
        } else {
          continue;
        }
        followInViews(item);
        const int before = view->property("currentIndex").toInt();
        QMetaObject::invokeMethod(view, method);
        const int after = view->property("currentIndex").toInt();
        if (after == before) return false;
        showIndex(view, after);
        if (auto* current = qvariant_cast<QQuickItem*>(view->property("currentItem")))
          current->forceActiveFocus(Qt::TabFocusReason);
        return true;
      }
      return false;
    }

    // contentX or contentY moved so [start, end) (in the viewport) fits in
    // [0, size), within the Flickable's bounds.
    void scrollAxis(QQuickItem* flickable, const char* position, double start, double end, double size,
                    double contentSize, double origin, double marginBefore, double marginAfter) {
      if (contentSize <= size) return;
      double margin = RevealMargin;
      if (end - start + 2 * margin > size) margin = 0;
      double shift = 0;
      if (start < margin)
        shift = start - margin;
      else if (end > size - margin)
        shift = std::min(end - size + margin, start - margin);
      if (shift == 0) return;
      const double current = flickable->property(position).toDouble();
      const double lowest = origin - marginBefore;
      const double highest = std::max(lowest, origin + contentSize + marginAfter - size);
      const double wanted = std::clamp(current + shift, lowest, highest);
      if (wanted != current) flickable->setProperty(position, wanted);
    }

    QQuickItem* findFlickableAround(QQuickItem* item, bool vertical) {
      for (QQuickItem* node = item->parentItem(); node; node = node->parentItem()) {
        if (!isFlickable(node)) continue;
        const double size = vertical ? node->height() : node->width();
        const double content = node->property(vertical ? "contentHeight" : "contentWidth").toDouble();
        if (content > size) return node;
      }
      return nullptr;
    }

    QPointF centreIn(QQuickItem* item, QQuickItem* reference) {
      return item->mapToItem(reference, QPointF(item->width() / 2, item->height() / 2));
    }
  } // namespace

  SpatialNavigation::SpatialNavigation(QObject* parent) : QObject(parent) {
    if (QQuickItem* attachee = item()) {
      connect(attachee, &QQuickItem::windowChanged, this, &SpatialNavigation::attachToWindow);
    }
  }

  SpatialNavigation::~SpatialNavigation() = default;

  SpatialNavigation* SpatialNavigation::qmlAttachedProperties(QObject* object) {
    return new SpatialNavigation(object);
  }

  QQuickItem* SpatialNavigation::item() const { return qobject_cast<QQuickItem*>(parent()); }

  void SpatialNavigation::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (QQuickItem* attachee = item()) {
      if (enabled)
        attachee->installEventFilter(this);
      else
        attachee->removeEventFilter(this);
    }
    attachToWindow();
    Q_EMIT enabledChanged();
  }

  void SpatialNavigation::attachToWindow() {
    QQuickItem* attachee = item();
    QQuickWindow* window = m_enabled && attachee ? attachee->window() : nullptr;
    if (window == m_window) return;
    if (m_window) m_window->removeEventFilter(this);
    disconnect(m_focusConnection);
    m_window = window;
    if (!window) return;
    window->installEventFilter(this);
    m_focusConnection = connect(window, &QQuickWindow::activeFocusItemChanged, this, &SpatialNavigation::onFocusChanged);
  }

  bool SpatialNavigation::directionOfKey(int key, Direction* direction) {
    switch (key) {
    case Qt::Key_Left: *direction = Left; return true;
    case Qt::Key_Right: *direction = Right; return true;
    case Qt::Key_Up: *direction = Up; return true;
    case Qt::Key_Down: *direction = Down; return true;
    default: return false;
    }
  }

  QQuickItem* SpatialNavigation::findNext(QQuickItem* scope, QQuickItem* from, Direction direction) {
    if (!scope || !scope->window()) return nullptr;
    const QRectF window(QPointF(0, 0), scope->window()->size());
    std::vector<Candidate> candidates;
    collect(scope, false, std::nullopt, window, candidates);
    if (candidates.empty()) return nullptr;

    // Nothing focused yet, or a scope with focusable items inside: the
    // first of those in reading order.
    if (!from || from == scope || !scope->isAncestorOf(from) ||
        std::none_of(candidates.begin(), candidates.end(), [from](const Candidate& c) { return c.item == from; })) {
      const QQuickItem* container = from && scope->isAncestorOf(from) ? from : scope;
      const Candidate* first = nullptr;
      for (const Candidate& candidate : candidates) {
        if (candidate.item == from || !container->isAncestorOf(candidate.item)) continue;
        if (!first || std::make_pair(std::round(candidate.rect.top()), candidate.rect.left()) <
                          std::make_pair(std::round(first->rect.top()), first->rect.left()))
          first = &candidate;
      }
      if (first) return first->item;
      if (!from || from == scope || !scope->isAncestorOf(from)) return nullptr;
    }

    const QRectF origin = sceneRect(from);
    QQuickItem* best = nullptr;
    double bestScore = std::numeric_limits<double>::max();
    for (const Candidate& candidate : candidates) {
      if (candidate.item == from) continue;
      // A scope the focus is already in would only take it back.
      if (candidate.item->isAncestorOf(from) && isFocusScope(candidate.item)) continue;
      const double distance = score(origin, candidate.rect, direction);
      if (distance >= 0 && distance < bestScore) {
        bestScore = distance;
        best = candidate.item;
      }
    }
    return best;
  }

  void SpatialNavigation::focusItem(QQuickItem* item) {
    if (!item) return;
    followInViews(item);
    // As by Tab, so the controls show their focus ring.
    item->forceActiveFocus(Qt::TabFocusReason);
    reveal(item);
  }

  void SpatialNavigation::reveal(QQuickItem* item) {
    for (QQuickItem* node = item->parentItem(); node; node = node->parentItem()) {
      // A view shows its current item itself (followInViews).
      if (!isFlickable(node) || isItemView(node)) continue;
      QQuickItem* content = contentItemOf(node);
      if (!content || !content->isAncestorOf(item)) continue;
      const QRectF rect = item->mapRectToItem(node, QRectF(0, 0, item->width(), item->height()));
      scrollAxis(node, "contentY", rect.top(), rect.bottom(), node->height(), node->property("contentHeight").toDouble(),
                 node->property("originY").toDouble(), node->property("topMargin").toDouble(),
                 node->property("bottomMargin").toDouble());
      scrollAxis(node, "contentX", rect.left(), rect.right(), node->width(), node->property("contentWidth").toDouble(),
                 node->property("originX").toDouble(), node->property("leftMargin").toDouble(),
                 node->property("rightMargin").toDouble());
    }
  }

  bool SpatialNavigation::move(Direction direction) {
    QQuickItem* scope = item();
    QQuickWindow* window = scope ? scope->window() : nullptr;
    if (!window) return false;
    QQuickItem* next = findNext(scope, window->activeFocusItem(), direction);
    if (!next) return false;
    focusItem(next);
    return true;
  }

  bool SpatialNavigation::step(Direction direction) {
    QQuickItem* scope = item();
    QQuickWindow* window = scope ? scope->window() : nullptr;
    QQuickItem* focused = window ? window->activeFocusItem() : nullptr;
    if (focused && scope->isAncestorOf(focused) && stepInView(focused, direction)) return true;
    return move(direction);
  }

  bool SpatialNavigation::movePage(Direction direction) {
    QQuickItem* scope = item();
    QQuickWindow* window = scope ? scope->window() : nullptr;
    QQuickItem* start = window ? window->activeFocusItem() : nullptr;
    if (!start || !scope->isAncestorOf(start)) return false;
    const bool vertical = direction == Up || direction == Down;
    // Only where something scrolls: elsewhere the arrows may mean other
    // things (the player's Up and Down are the volume).
    QQuickItem* flickable = findFlickableAround(start, vertical);
    QQuickItem* reference = flickable ? contentItemOf(flickable) : nullptr;
    if (!reference) return false;
    const double page = vertical ? flickable->height() : flickable->width();
    const QPointF from = centreIn(start, reference);
    // Step by step, through the views' own navigation and this one. No key
    // is sent: the focused item (an editor whose arrows change its value)
    // never sees one.
    QPointer<QQuickItem> current = start;
    bool moved = false;
    for (int i = 0; i < MaxPageSteps; ++i) {
      if (!step(direction)) break;
      QQuickItem* now = window->activeFocusItem();
      if (!now || now == current) break;
      // Out of the scrolled area (the mini player under a page): one step
      // back, the page move ends at its edge.
      if (!flickable->isAncestorOf(now)) {
        if (current) focusItem(current);
        break;
      }
      moved = true;
      current = now;
      const QPointF here = centreIn(now, reference);
      const double travelled = vertical ? std::abs(here.y() - from.y()) : std::abs(here.x() - from.x());
      if (travelled >= page * PageShare) break;
    }
    return moved;
  }

  void SpatialNavigation::onFocusChanged() {
    // Only for keys: a click focuses what is already in view.
    if (!m_enabled || !m_fromKeys || !m_window) return;
    QQuickItem* focused = m_window->activeFocusItem();
    QQuickItem* scope = item();
    if (!focused || !scope || !scope->isAncestorOf(focused)) return;
    followInViews(focused);
    reveal(focused);
  }

  bool SpatialNavigation::handleKey(QKeyEvent* event, bool fromWindow) {
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (modifiers != Qt::NoModifier) return false;
    QQuickItem* scope = item();
    QQuickWindow* window = scope ? scope->window() : nullptr;
    if (!window) return false;
    const int key = event->key();
    Direction direction = Left;
    if (fromWindow) {
      // Keys a control gives up (navigationKeys) move the focus before the
      // control sees them, even when there is nowhere to go.
      QQuickItem* focused = window->activeFocusItem();
      if (!focused || !scope->isAncestorOf(focused) || !directionOfKey(key, &direction)) return false;
      QVariant keys = focused->property("navigationKeys");
      if (!keys.isValid()) return false;
      // A QML `var` property holds a JavaScript array.
      if (keys.metaType() == QMetaType::fromType<QJSValue>()) keys = keys.value<QJSValue>().toVariant();
      const QVariantList list = keys.toList();
      const bool listed = std::any_of(list.begin(), list.end(), [key](const QVariant& k) { return k.toInt() == key; });
      if (!listed) return false;
      step(direction);
      return true;
    }
    if (directionOfKey(key, &direction)) return move(direction);
    if (key == Qt::Key_PageDown) return movePage(Down);
    if (key == Qt::Key_PageUp) return movePage(Up);
    return false;
  }

  bool SpatialNavigation::eventFilter(QObject* watched, QEvent* event) {
    if (!m_enabled) return false;
    const bool fromWindow = watched == m_window;
    switch (event->type()) {
    case QEvent::KeyPress:
      if (fromWindow) m_fromKeys = true;
      return handleKey(static_cast<QKeyEvent*>(event), fromWindow);
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
      if (fromWindow) m_fromKeys = false;
      return false;
    default: return false;
    }
  }

} // namespace jfd::ui
