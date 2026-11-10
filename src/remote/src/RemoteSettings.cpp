#include <jfd/remote/RemoteSettings.h>

#include <QCoreApplication>

namespace jfd::remote {

  bool registerRemoteSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("remote");
    section.context = "RemoteSettings";
    section.title = QT_TRANSLATE_NOOP("RemoteSettings", "Remote control");
    section.order = 115;

    settings::SettingDescriptor allow;
    allow.key = QString(keys::AllowServerControl).section(QLatin1Char('/'), 1);
    allow.type = settings::SettingType::Bool;
    allow.defaultValue = true;
    allow.label = QT_TRANSLATE_NOOP("RemoteSettings", "Allow control from other devices");
    allow.description = QT_TRANSLATE_NOOP("RemoteSettings", "Jellyfin on your phone or in a browser, signed in as "
                                                            "you, can play, pause and seek here and change the "
                                                            "volume and tracks (\"Play on\").");
    section.settings.append(allow);
    return registry.registerSection(std::move(section));
  }

} // namespace jfd::remote
