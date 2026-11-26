#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <qqmlintegration.h>

// Text entry without a keyboard, for the on-screen keyboard of TV mode and
// of a gamepad in use, Theme.focusDriven (OnScreenKeyboard.qml draws it;
// this decides when it shows and where its keys go). Watches the window it
// is in:
//
// - the focused editable text field (a TextInput, as in InputField) is
//   `field`;
// - while `active` (focus-driven, or open), the keyboard opens when the focus reaches a
//   field by an arrow, Tab or a page key, and on Select (Enter) on the
//   field, unless the field's `keyboardOnSelect` property (InputField's) is
//   false;
// - once a physical keyboard types (a real key press with text, or
//   Backspace), it stays closed and Enter on a field is Enter again, until
//   a gamepad or the phone (InputRouter's synthetic keys) is used;
// - while open, the arrows, Select and Back belong to the keyboard
//   (`keyPressed`), PageUp erases and PageDown types a space (a gamepad's
//   shoulder buttons), Back closes it. The field keeps the focus all the
//   time, so its cursor shows, Tab still works and text from the phone
//   (SendString) arrives where it would without the keyboard.
namespace jfd::ui {

  class TextEntry : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QQuickItem* field READ field NOTIFY fieldChanged)
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(bool keyboardTyping READ keyboardTyping NOTIFY keyboardTypingChanged)

  public:
    explicit TextEntry(QQuickItem* parent = nullptr);
    ~TextEntry() override;

    bool isActive() const { return m_active; }
    void setActive(bool active);
    QQuickItem* field() const { return m_field; }
    bool isOpen() const { return m_open; }
    bool keyboardTyping() const { return m_keyboardTyping; }

    // Opens for the focused field; nothing without one or when not active.
    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    // Into the field as typed (replacing a selection).
    Q_INVOKABLE void type(const QString& text);
    // Backspace in the field.
    Q_INVOKABLE void erase();
    // The cursor one character left (-1) or right (1).
    Q_INVOKABLE void moveCursor(int step);
    // Enter in the field, as the keyboard's Done: what the field does on
    // Enter happens. The keyboard stays open when that moved the focus to
    // another field (user name, then password), else it closes.
    Q_INVOKABLE void done();

    // An enabled, editable single-line text field.
    static bool isTextField(const QQuickItem* item);

  Q_SIGNALS:
    void activeChanged();
    void fieldChanged();
    void openChanged();
    void keyboardTypingChanged();
    // While open: an arrow, Return, Enter or Select for the keyboard's own
    // highlight. Back, PageUp and PageDown are handled here.
    void keyPressed(int key, bool autoRepeat);

  protected:
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void attachToWindow(QQuickWindow* window);
    void onFocusChanged();
    void setOpen(bool open);
    void setKeyboardTyping(bool typing);
    bool handleKey(QKeyEvent* event);

    bool m_active = false;
    bool m_open = false;
    bool m_keyboardTyping = false;
    // A navigation key is being delivered: a field focused now was reached
    // by keys.
    bool m_navigating = false;
    // Inside done(): its Enter passes, and a new field keeps the keyboard.
    bool m_inDone = false;
    QPointer<QQuickItem> m_field;
    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_focusConnection;
  };

} // namespace jfd::ui
