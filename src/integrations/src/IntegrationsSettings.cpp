#include <jfd/integrations/IntegrationsSettings.h>

#include <QCoreApplication>

namespace jfd::integrations {

  namespace {
    QString key(QLatin1String fullKey) {
      return QString(fullKey).section(QLatin1Char('/'), 1);
    }
  } // namespace

  bool registerIntegrationsSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("integrations");
    section.context = "IntegrationsSettings";
    section.title = QT_TRANSLATE_NOOP("IntegrationsSettings", "Integrations");
    section.order = 900;

    settings::SettingDescriptor discord;
    discord.key = key(keys::Discord);
    discord.type = settings::SettingType::Bool;
    discord.defaultValue = false;
    discord.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Show what I watch in Discord");
    discord.description = QT_TRANSLATE_NOOP(
        "IntegrationsSettings",
        "The Discord app on this computer shows the title, and \"Watching together\" in a group.");
    section.settings.append(discord);

    settings::SettingDescriptor updates;
    updates.key = key(keys::CheckForUpdates);
    updates.type = settings::SettingType::Bool;
    updates.defaultValue = true;
    updates.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Check for a new version at startup");
    updates.description = QT_TRANSLATE_NOOP("IntegrationsSettings",
                                            "Only shows a link to the download page; nothing is installed.");
    section.settings.append(updates);

    return registry.registerSection(section);
  }

} // namespace jfd::integrations
