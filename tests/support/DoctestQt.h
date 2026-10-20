#pragma once

#include <doctest/doctest.h>

#include <QByteArray>
#include <QString>
#include <QUrl>

// Lets CHECK print Qt values when a comparison fails.
namespace doctest {

  template <> struct StringMaker<QString> {
    static String convert(const QString& value) {
      return String(('"' + value.toUtf8() + '"').constData());
    }
  };

  template <> struct StringMaker<QByteArray> {
    static String convert(const QByteArray& value) {
      return String(('"' + value + '"').constData());
    }
  };

  template <> struct StringMaker<QUrl> {
    static String convert(const QUrl& value) {
      return String(value.toString().toUtf8().constData());
    }
  };

} // namespace doctest
