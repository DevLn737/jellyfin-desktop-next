#include "TextEntry.h"

#include <QCoreApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTimer>

#include <algorithm>

namespace jfd::ui {

  namespace {
    bool isArrow(int key) { return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down; }

    bool isSelect(int key) { return key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Select; }

    // Keys that move the focus: a field they focus was reached by keys.
    bool isNavigation(int key) {
      return isArrow(key) || key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_PageUp ||
             key == Qt::Key_PageDown;
    }

    // A key that writes: a printable character (Space included), or
    // Backspace and Delete.
    bool writes(const QKeyEvent* event) {
      if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
      if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) return true;
      const QString text = event->text();
      return !text.isEmpty() && text.at(0).isPrint();
    }
  } // namespace

  TextEntry::TextEntry(QQuickItem* parent) : QQuickItem(parent) {}

  TextEntry::~TextEntry() {
    if (m_window) m_window->removeEventFilter(this);
  }

  bool TextEntry::isTextField(const QQuickItem* item) {
    return item != nullptr && item->inherits("QQuickTextInput") && item->isEnabled() &&
           !item->property("readOnly").toBool();
  }

  void TextEntry::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    if (!active) setOpen(false);
    setKeyboardTyping(false);
    Q_EMIT activeChanged();
  }

  void TextEntry::setOpen(bool open) {
    if (open && (!m_active || !m_field)) return;
    if (m_open == open) return;
    m_open = open;
    Q_EMIT openChanged();
  }

  void TextEntry::setKeyboardTyping(bool typing) {
    if (m_keyboardTyping == typing) return;
    m_keyboardTyping = typing;
    Q_EMIT keyboardTypingChanged();
  }

  void TextEntry::show() { setOpen(true); }

  void TextEntry::hide() { setOpen(false); }

  void TextEntry::type(const QString& text) {
    if (!m_field || text.isEmpty()) return;
    // As an input method commits text: the field treats it as typed
    // (selection replaced, validator and maximum length applied, textEdited).
    QInputMethodEvent event;
    event.setCommitString(text);
    QCoreApplication::sendEvent(m_field, &event);
  }

  void TextEntry::erase() {
    if (!m_field) return;
    // To the field itself: past this filter and the window's shortcuts.
    QPointer<QQuickItem> field = m_field;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier, QStringLiteral("\b"));
    QCoreApplication::sendEvent(field, &press);
    if (!field) return;
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Backspace, Qt::NoModifier, QStringLiteral("\b"));
    QCoreApplication::sendEvent(field, &release);
  }

  void TextEntry::moveCursor(int step) {
    if (!m_field) return;
    const int position = m_field->property("cursorPosition").toInt();
    const int length = m_field->property("length").toInt();
    m_field->setProperty("cursorPosition", std::clamp(position + step, 0, length));
  }

  void TextEntry::done() {
    if (!m_field || !m_window) {
      setOpen(false);
      return;
    }
    QPointer<QQuickItem> before = m_field;
    QPointer<QQuickWindow> window = m_window;
    m_inDone = true;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCoreApplication::sendEvent(window, &press);
    if (window) {
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
      QCoreApplication::sendEvent(window, &release);
    }
    m_inDone = false;
    // Enter moved on to another field: the keyboard follows it.
    if (!m_field || m_field == before) setOpen(false);
  }

  void TextEntry::itemChange(ItemChange change, const ItemChangeData& data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) attachToWindow(data.window);
  }

  void TextEntry::attachToWindow(QQuickWindow* window) {
    if (window == m_window) return;
    if (m_window) m_window->removeEventFilter(this);
    disconnect(m_focusConnection);
    m_window = window;
    if (window) {
      window->installEventFilter(this);
      m_focusConnection = connect(window, &QQuickWindow::activeFocusItemChanged, this, &TextEntry::onFocusChanged);
    }
    onFocusChanged();
  }

  void TextEntry::onFocusChanged() {
    QQuickItem* focused = m_window ? m_window->activeFocusItem() : nullptr;
    QQuickItem* next = isTextField(focused) ? focused : nullptr;
    if (next == m_field) return;
    m_field = next;
    Q_EMIT fieldChanged();
    if (!next) {
      setOpen(false);
      return;
    }
    // Open: it follows to the next field. Closed: it opens only for a
    // field reached by keys, not for one a page focuses as it opens or one
    // clicked with the mouse.
    if (!m_open && m_navigating && !m_keyboardTyping) setOpen(true);
  }

  bool TextEntry::handleKey(QKeyEvent* event) {
    // A physical keyboard's keys come from the system; the gamepad's and
    // the phone's (InputRouter) and done()'s are sent by the application.
    const bool physical = event->spontaneous();
    if (!physical && !m_inDone) setKeyboardTyping(false);
    if (physical && writes(event)) {
      setKeyboardTyping(true);
      setOpen(false);
      return false;
    }
    if (m_inDone || !m_active) return false;
    if ((event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier) return false;
    const int key = event->key();
    if (m_open) {
      if (isArrow(key) || isSelect(key)) {
        Q_EMIT keyPressed(key, event->isAutoRepeat());
        return true;
      }
      switch (key) {
      case Qt::Key_Escape:
      case Qt::Key_Back: setOpen(false); return true;
      case Qt::Key_PageUp:
      case Qt::Key_Backspace: erase(); return true;
      case Qt::Key_PageDown: type(QStringLiteral(" ")); return true;
      default: return false;
      }
    }
    // A field can keep Select for itself (keyboardOnSelect: false).
    const QVariant onSelect = m_field ? m_field->property("keyboardOnSelect") : QVariant();
    if (m_field && !m_keyboardTyping && isSelect(key) && (!onSelect.isValid() || onSelect.toBool())) {
      setOpen(true);
      return true;
    }
    if (isNavigation(key)) {
      // Until this key is delivered: a field it focuses opens the keyboard.
      m_navigating = true;
      QTimer::singleShot(0, this, [this] { m_navigating = false; });
    }
    return false;
  }

  bool TextEntry::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_window || event->type() != QEvent::KeyPress) return false;
    return handleKey(static_cast<QKeyEvent*>(event));
  }

} // namespace jfd::ui
