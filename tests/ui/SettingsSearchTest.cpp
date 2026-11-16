#include "../support/DoctestQt.h"

#include <jfd/settings/Settings.h>
#include <jfd/ui/SettingsModel.h>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTranslator>

using namespace jfd::settings;
using jfd::ui::SettingsModel;
using jfd::ui::SettingsSearch;

namespace {

  // Russian for the few words below.
  class Russian : public QTranslator {
  public:
    bool isEmpty() const override { return false; }
    QString translate(const char*, const char* source, const char*, int) const override {
      const QString text = QString::fromUtf8(source);
      if (text == QLatin1String("Playback")) return QStringLiteral("Воспроизведение");
      if (text == QLatin1String("Volume")) return QStringLiteral("Громкость");
      if (text == QLatin1String("Subtitle style")) return QStringLiteral("Стиль субтитров");
      if (text == QLatin1String("Large")) return QStringLiteral("Крупный");
      return {};
    }
  };

  SectionDescriptor playback() {
    SectionDescriptor section;
    section.id = QStringLiteral("playback");
    section.context = "Playback";
    section.title = "Playback";
    section.version = 1;

    SettingDescriptor volume;
    volume.key = QStringLiteral("volume");
    volume.type = SettingType::Int;
    volume.defaultValue = 100;
    volume.label = "Volume";
    volume.description = "How loud a film starts.";
    volume.minimum = 0;
    volume.maximum = 130;
    section.settings.append(volume);

    SettingDescriptor style;
    style.key = QStringLiteral("subtitles");
    style.type = SettingType::Choice;
    style.defaultValue = QStringLiteral("normal");
    style.label = "Subtitle style";
    style.choices = {{QStringLiteral("normal"), "Normal"}, {QStringLiteral("large"), "Large"}};
    section.settings.append(style);
    return section;
  }

  QStringList keys(const SettingsSearch& search) {
    QStringList out;
    for (int row = 0; row < search.rowCount(); ++row)
      out.append(search.index(row, 0).data(SettingsModel::KeyRole).toString());
    return out;
  }

} // namespace

TEST_CASE("The settings search matches words in either language, in any case") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  SettingsRegistry registry;
  REQUIRE(registry.registerSection(playback()));
  Settings settings(registry, dir.filePath(QStringLiteral("settings.ini")));
  Russian russian;
  QCoreApplication::installTranslator(&russian);

  SettingsModel model(settings);
  SettingsSearch search;
  search.setSettings(&model);
  CHECK(search.count() == 2);

  search.setText(QStringLiteral("громк"));
  CHECK(keys(search) == QStringList{QStringLiteral("playback/volume")});
  search.setText(QStringLiteral("VOLUME"));
  CHECK(keys(search) == QStringList{QStringLiteral("playback/volume")});
  // The description, a choice, the section's title.
  search.setText(QStringLiteral("loud"));
  CHECK(keys(search) == QStringList{QStringLiteral("playback/volume")});
  search.setText(QStringLiteral("крупный"));
  CHECK(keys(search) == QStringList{QStringLiteral("playback/subtitles")});
  search.setText(QStringLiteral("воспроизведение"));
  CHECK(search.count() == 2);
  // Every word must be there, in any order.
  search.setText(QStringLiteral("субтитров стиль"));
  CHECK(keys(search) == QStringList{QStringLiteral("playback/subtitles")});
  search.setText(QStringLiteral("стиль громкость"));
  CHECK(search.count() == 0);
  search.setText(QStringLiteral("  "));
  CHECK(search.count() == 2);

  QCoreApplication::removeTranslator(&russian);
}
