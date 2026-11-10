#pragma once

#include <jfd/platform/InputAction.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <qqmlintegration.h>

// The gamepads for the interface: whether one is connected and its name
// (the shell's gamepad sign, the settings page), and the button check of
// the settings page ("press any button"). The application feeds it from
// GamepadInput (InputInstall.cpp); without SDL it stays unavailable.
//
// During the check the presses are shown, not acted on: every press names
// its action (lastAction), and Back twice in a row ends the check, so a
// gamepad alone can leave it.
namespace jfd::ui {

  class GamepadStatus : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // The build has gamepad support and it is switched on.
    Q_PROPERTY(bool available READ available NOTIFY controllersChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY controllersChanged)
    // The first connected gamepad's name as SDL reports it; empty for none.
    Q_PROPERTY(QString controllerName READ controllerName NOTIFY controllersChanged)
    Q_PROPERTY(QStringList controllerNames READ controllerNames NOTIFY controllersChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    // The action of the last press during the check, translated; empty
    // before the first.
    Q_PROPERTY(QString lastAction READ lastAction NOTIFY pressed)
    Q_PROPERTY(int pressCount READ pressCount NOTIFY pressed)

  public:
    explicit GamepadStatus(QObject* parent = nullptr);

    bool available() const { return m_available; }
    bool connected() const { return !m_names.isEmpty(); }
    QString controllerName() const { return m_names.value(0); }
    QStringList controllerNames() const { return m_names; }
    bool checking() const { return m_checking; }
    QString lastAction() const { return m_lastAction; }
    int pressCount() const { return m_pressCount; }

    void setAvailable(bool available);
    void setControllers(const QStringList& names);

    // A press from the gamepad. During the check it is shown and taken
    // (true): the caller does not act on it. Otherwise false.
    bool takePress(platform::InputAction action, bool repeat);

    Q_INVOKABLE void startCheck();
    Q_INVOKABLE void stopCheck();

    // "Select", "Back", ... in the interface language.
    static QString actionText(platform::InputAction action);

  Q_SIGNALS:
    void controllersChanged();
    void checkingChanged();
    void pressed();

  private:
    bool m_available = false;
    QStringList m_names;
    bool m_checking = false;
    QString m_lastAction;
    int m_pressCount = 0;
    bool m_lastWasBack = false;
  };

} // namespace jfd::ui
