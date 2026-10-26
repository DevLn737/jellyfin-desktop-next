#include "jfd/ui/CommandRegistry.h"

#include <QVariantMap>

#include <algorithm>

namespace jfd::ui {

  CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent) {}

  void CommandRegistry::add(Command command) {
    if (command.id.isEmpty() || !command.run) {
      return;
    }
    const auto same = [&](const Command& other) { return other.id == command.id; };
    if (const auto existing = std::find_if(m_commands.begin(), m_commands.end(), same);
        existing != m_commands.end()) {
      *existing = std::move(command);
    } else {
      m_commands.append(std::move(command));
    }
    Q_EMIT changed();
  }

  void CommandRegistry::remove(const QString& id) {
    const auto removed = m_commands.removeIf([&](const Command& command) { return command.id == id; });
    if (removed > 0) {
      Q_EMIT changed();
    }
  }

  bool CommandRegistry::contains(const QString& id) const {
    return std::any_of(m_commands.begin(), m_commands.end(), [&](const Command& c) { return c.id == id; });
  }

  bool CommandRegistry::isAvailable(const Command& command) const {
    return !command.available || command.available();
  }

  QVariantList CommandRegistry::commands(const QString& filter) const {
    QVariantList list;
    for (const Command& command : m_commands) {
      if (isAvailable(command) && (filter.isEmpty() || command.title.contains(filter, Qt::CaseInsensitive))) {
        list.append(QVariantMap{{QStringLiteral("id"), command.id}, {QStringLiteral("title"), command.title}});
      }
    }
    return list;
  }

  bool CommandRegistry::run(const QString& id) {
    for (const Command& command : m_commands) {
      if (command.id == id) {
        if (!isAvailable(command)) {
          return false;
        }
        // A copy: the command may change the registry.
        const auto run = command.run;
        run();
        return true;
      }
    }
    return false;
  }

} // namespace jfd::ui
