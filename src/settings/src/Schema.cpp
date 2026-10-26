#include "jfd/settings/Schema.h"

#include <QMetaType>

#include <climits>
#include <cmath>

namespace jfd::settings {

  namespace {
    bool inRange(const SettingDescriptor& setting, double value) {
      if (setting.minimum == setting.maximum) {
        return true;
      }
      return value >= setting.minimum && value <= setting.maximum;
    }
  } // namespace

  QVariant coerce(const SettingDescriptor& setting, const QVariant& value) {
    if (!value.isValid()) {
      return {};
    }
    switch (setting.type) {
    case SettingType::Bool: {
      // INI files store booleans as "true"/"false".
      if (value.typeId() == QMetaType::Bool) {
        return value;
      }
      const QString text = value.toString().trimmed().toLower();
      if (text == QLatin1String("true") || text == QLatin1String("1")) {
        return true;
      }
      if (text == QLatin1String("false") || text == QLatin1String("0")) {
        return false;
      }
      return {};
    }
    case SettingType::Int: {
      if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::trunc(number) != number) {
          return {};
        }
      }
      bool ok = false;
      const qlonglong number = value.toLongLong(&ok);
      if (!ok || number < INT_MIN || number > INT_MAX || !inRange(setting, double(number))) {
        return {};
      }
      return QVariant(int(number));
    }
    case SettingType::Double: {
      bool ok = false;
      const double number = value.toDouble(&ok);
      if (!ok || !std::isfinite(number) || !inRange(setting, number)) {
        return {};
      }
      return QVariant(number);
    }
    case SettingType::String:
      if (!value.canConvert<QString>()) {
        return {};
      }
      return QVariant(value.toString());
    case SettingType::Choice: {
      const QString text = value.toString();
      for (const Choice& choice : choicesOf(setting)) {
        if (choice.value == text) {
          return QVariant(text);
        }
      }
      return {};
    }
    case SettingType::Action:
      return {};
    }
    return {};
  }

  QList<Choice> choicesOf(const SettingDescriptor& setting) {
    return setting.dynamicChoices ? setting.dynamicChoices() : setting.choices;
  }

} // namespace jfd::settings
