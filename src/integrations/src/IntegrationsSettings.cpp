#include <jfd/integrations/IntegrationsSettings.h>

#include <QCoreApplication>

#include <utility>

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

    settings::SettingDescriptor hidden;
    hidden.key = key(keys::DiscordHidden);
    hidden.type = settings::SettingType::Bool;
    hidden.defaultValue = false;
    hidden.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Hide from Discord for now");
    hidden.description = QT_TRANSLATE_NOOP(
        "IntegrationsSettings",
        "Nothing is shown until you turn this off. The player's settings menu has the same switch.");
    section.settings.append(hidden);

    settings::SettingDescriptor detail;
    detail.key = key(keys::DiscordDetail);
    detail.type = settings::SettingType::Choice;
    detail.defaultValue = QStringLiteral("full");
    detail.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "What Discord shows");
    detail.choices = {
        {QStringLiteral("full"), QT_TRANSLATE_NOOP("IntegrationsSettings", "Title, episode and time")},
        {QStringLiteral("kind"), QT_TRANSLATE_NOOP("IntegrationsSettings", "Only \"Watching a movie\", no title")},
    };
    section.settings.append(detail);

    const std::pair<QLatin1String, const char*> kinds[] = {
        {keys::DiscordMovies, QT_TRANSLATE_NOOP("IntegrationsSettings", "Show movies in Discord")},
        {keys::DiscordSeries, QT_TRANSLATE_NOOP("IntegrationsSettings", "Show series in Discord")},
        {keys::DiscordMusic, QT_TRANSLATE_NOOP("IntegrationsSettings", "Show music in Discord")},
        {keys::DiscordLiveTv, QT_TRANSLATE_NOOP("IntegrationsSettings", "Show TV channels in Discord")},
    };
    for (const auto& [kind, label] : kinds) {
      settings::SettingDescriptor shown;
      shown.key = key(kind);
      shown.label = label;
      shown.type = settings::SettingType::Bool;
      shown.defaultValue = true;
      shown.hidden = true;
      section.settings.append(shown);
    }
    settings::SettingDescriptor libraries;
    libraries.key = key(keys::DiscordHiddenLibraries);
    libraries.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Libraries hidden from Discord");
    libraries.type = settings::SettingType::String;
    libraries.defaultValue = QString();
    libraries.hidden = true;
    section.settings.append(libraries);

    settings::SettingDescriptor choose;
    choose.key = key(keys::DiscordChoose);
    choose.type = settings::SettingType::Action;
    choose.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Choose what to show in Discord");
    choose.description = QT_TRANSLATE_NOOP(
        "IntegrationsSettings", "Movies, series, music and TV, and libraries that are never shown.");
    section.settings.append(choose);

    settings::SettingDescriptor poster;
    poster.key = key(keys::DiscordPoster);
    poster.type = settings::SettingType::Bool;
    poster.defaultValue = false;
    poster.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Show the poster in Discord");
    poster.description = QT_TRANSLATE_NOOP(
        "IntegrationsSettings",
        "The poster comes from the open databases your server uses (TMDB, MusicBrainz). When they "
        "have none, Discord loads it from your server, and people who see your status can see the "
        "server's address; that works only for servers with a public https address.");
    section.settings.append(poster);

    settings::SettingDescriptor updates;
    updates.key = key(keys::CheckForUpdates);
    updates.type = settings::SettingType::Bool;
    updates.defaultValue = true;
    updates.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Check for updates at startup");
    updates.description = QT_TRANSLATE_NOOP(
        "IntegrationsSettings",
        "A new version is downloaded and its signature checked; it is installed only when you choose "
        "\"Restart and update\". Builds that cannot update themselves show a link to the download page.");
    section.settings.append(updates);

    settings::SettingDescriptor channel;
    channel.key = key(keys::UpdateChannel);
    channel.type = settings::SettingType::Choice;
    // The first builds for the group are pre-releases.
    channel.defaultValue = QStringLiteral("beta");
    channel.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Update channel");
    channel.choices = {
        {QStringLiteral("beta"), QT_TRANSLATE_NOOP("IntegrationsSettings", "Beta (pre-release versions too)")},
        {QStringLiteral("stable"), QT_TRANSLATE_NOOP("IntegrationsSettings", "Stable")},
    };
    section.settings.append(channel);

    settings::SettingDescriptor checkNow;
    checkNow.key = key(keys::CheckNow);
    checkNow.type = settings::SettingType::Action;
    checkNow.label = QT_TRANSLATE_NOOP("IntegrationsSettings", "Check for updates");
    checkNow.description = QT_TRANSLATE_NOOP(
        "IntegrationsSettings",
        "Looks for a newer version in the chosen channel now and says what it found.");
    section.settings.append(checkNow);

    return registry.registerSection(section);
  }

} // namespace jfd::integrations
