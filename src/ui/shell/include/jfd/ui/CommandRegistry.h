#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <qqmlintegration.h>

#include <functional>

namespace jfd::ui {

  // The commands of the Ctrl+K palette. Each module registers its own at
  // startup ("Open Music", "Open Requests"); the palette lists those that
  // are available at that moment, in registration order.
  //
  // Titles are shown as given, so a module passes them already translated.
  class CommandRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

  public:
    struct Command {
      // Stable; "navigate.music". A command with the same id replaces the
      // earlier one.
      QString id;
      QString title;
      // Empty: always available.
      std::function<bool()> available;
      std::function<void()> run;
    };

    explicit CommandRegistry(QObject* parent = nullptr);

    void add(Command command);
    void remove(const QString& id);
    bool contains(const QString& id) const;

    // The available commands whose title contains `filter` (ignoring
    // case; an empty filter lists all): maps with "id" and "title".
    Q_INVOKABLE QVariantList commands(const QString& filter = {}) const;
    // Runs a command if it is available. False for an unknown or
    // unavailable id.
    Q_INVOKABLE bool run(const QString& id);

  Q_SIGNALS:
    void changed();

  private:
    bool isAvailable(const Command& command) const;

    QList<Command> m_commands;
  };

} // namespace jfd::ui
