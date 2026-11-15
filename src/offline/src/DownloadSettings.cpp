#include <jfd/offline/DownloadSettings.h>

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace jfd::offline {

  QString defaultDownloadFolder() {
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty()) videos = QDir::homePath() + QStringLiteral("/Videos");
    return QDir::cleanPath(videos + QStringLiteral("/Jellyfin"));
  }

  bool registerDownloadSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("downloads");
    section.context = "DownloadSettings";
    section.title = QT_TRANSLATE_NOOP("DownloadSettings", "Downloads");
    section.order = 120;

    const auto key = [](QLatin1String full) { return QString(full).section(QLatin1Char('/'), 1); };

    settings::SettingDescriptor folder;
    folder.key = key(keys::Folder);
    folder.type = settings::SettingType::Folder;
    folder.defaultValue = QDir::toNativeSeparators(defaultDownloadFolder());
    folder.label = QT_TRANSLATE_NOOP("DownloadSettings", "Download folder");
    folder.description = QT_TRANSLATE_NOOP("DownloadSettings",
                                           "Where downloaded movies, episodes and music are kept. "
                                           "Downloads already made stay in the old folder and are "
                                           "shown again when you choose it again.");
    folder.requiresRestart = true;
    section.settings.append(folder);

    settings::SettingDescriptor limit;
    limit.key = key(keys::MaxGiB);
    limit.type = settings::SettingType::Int;
    limit.defaultValue = 100;
    limit.minimum = 0;
    limit.maximum = 100000;
    limit.step = 10;
    limit.unit = QT_TRANSLATE_NOOP("DownloadSettings", "GiB");
    limit.label = QT_TRANSLATE_NOOP("DownloadSettings", "Space for downloads");
    limit.description = QT_TRANSLATE_NOOP(
        "DownloadSettings", "Downloads use at most this much disk space. 0: no limit.");
    section.settings.append(limit);

    settings::SettingDescriptor reserve;
    reserve.key = key(keys::ReserveGiB);
    reserve.type = settings::SettingType::Int;
    reserve.defaultValue = 5;
    reserve.minimum = 0;
    reserve.maximum = 1000;
    reserve.unit = QT_TRANSLATE_NOOP("DownloadSettings", "GiB");
    reserve.label = QT_TRANSLATE_NOOP("DownloadSettings", "Keep free on the disk");
    reserve.description = QT_TRANSLATE_NOOP(
        "DownloadSettings", "A download waits instead of filling the disk past this "
                            "much free space.");
    section.settings.append(reserve);

    settings::SettingDescriptor parallel;
    parallel.key = key(keys::MaxParallel);
    parallel.type = settings::SettingType::Int;
    parallel.defaultValue = 1;
    parallel.minimum = 1;
    parallel.maximum = 3;
    parallel.step = 1;
    parallel.label = QT_TRANSLATE_NOOP("DownloadSettings", "Downloads at the same time");
    parallel.description = QT_TRANSLATE_NOOP(
        "DownloadSettings", "More than one at a time can be faster on a fast server, and is "
                            "harder on it.");
    section.settings.append(parallel);

    settings::SettingDescriptor resume;
    resume.key = key(keys::ResumeOnStart);
    resume.type = settings::SettingType::Bool;
    resume.defaultValue = true;
    resume.label = QT_TRANSLATE_NOOP("DownloadSettings", "Continue downloads when the app starts");
    resume.description = QT_TRANSLATE_NOOP(
        "DownloadSettings", "Off: unfinished downloads wait, paused, until you continue them on "
                            "the Downloads page.");
    section.settings.append(resume);

    settings::SettingDescriptor preferLocal;
    preferLocal.key = key(keys::PreferLocal);
    preferLocal.type = settings::SettingType::Bool;
    preferLocal.defaultValue = true;
    preferLocal.label = QT_TRANSLATE_NOOP("DownloadSettings", "Play downloads from disk");
    preferLocal.description = QT_TRANSLATE_NOOP(
        "DownloadSettings", "A downloaded item plays from its file even when the server can be "
                            "reached. Without the server it always does.");
    section.settings.append(preferLocal);

    return registry.registerSection(std::move(section));
  }

} // namespace jfd::offline
