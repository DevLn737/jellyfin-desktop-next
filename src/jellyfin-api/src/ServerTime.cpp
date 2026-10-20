#include <jfd/api/ServerTime.h>

#include <chrono>
#include <cstdio>

namespace jfd::api {

  namespace {
    constexpr std::int64_t PerSecond = 10'000'000;

    // Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
    // days_from_civil), so parsing does not depend on the local time zone.
    constexpr std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
      y -= m <= 2 ? 1 : 0;
      const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
      const auto yoe = static_cast<unsigned>(y - era * 400);
      const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;
      const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
    }

    struct Civil {
      std::int64_t year;
      unsigned month;
      unsigned day;
    };

    constexpr Civil civilFromDays(std::int64_t z) {
      z += 719468;
      const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
      const auto doe = static_cast<unsigned>(z - era * 146097);
      const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
      const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
      const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
      const unsigned mp = (5 * doy + 2) / 153;
      const unsigned d = doy - (153 * mp + 2) / 5 + 1;
      const unsigned m = mp < 10 ? mp + 3 : mp - 9;
      return {y + (m <= 2 ? 1 : 0), m, d};
    }

    bool readDigits(QStringView text, qsizetype& pos, int count, int& out) {
      if (pos + count > text.size()) return false;
      int value = 0;
      for (int i = 0; i < count; ++i) {
        const QChar c = text[pos + i];
        if (!c.isDigit()) return false;
        value = value * 10 + c.digitValue();
      }
      pos += count;
      out = value;
      return true;
    }

    bool expect(QStringView text, qsizetype& pos, char c) {
      if (pos >= text.size() || text[pos] != QLatin1Char(c)) return false;
      ++pos;
      return true;
    }
  } // namespace

  core::ServerInstant systemClockNow() {
    using namespace std::chrono;
    return core::ServerInstant::fromSinceOrigin(
        duration_cast<core::WallTicks>(system_clock::now().time_since_epoch()));
  }

  std::optional<core::ServerInstant> parseServerInstant(QStringView text) {
    qsizetype pos = 0;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!readDigits(text, pos, 4, year) || !expect(text, pos, '-') ||
        !readDigits(text, pos, 2, month) || !expect(text, pos, '-') ||
        !readDigits(text, pos, 2, day))
      return std::nullopt;
    if (pos >= text.size() || (text[pos] != QLatin1Char('T') && text[pos] != QLatin1Char(' ')))
      return std::nullopt;
    ++pos;
    if (!readDigits(text, pos, 2, hour) || !expect(text, pos, ':') ||
        !readDigits(text, pos, 2, minute) || !expect(text, pos, ':') ||
        !readDigits(text, pos, 2, second))
      return std::nullopt;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59)
      return std::nullopt;

    std::int64_t fraction = 0;
    if (pos < text.size() && text[pos] == QLatin1Char('.')) {
      ++pos;
      int digits = 0;
      while (pos < text.size() && text[pos].isDigit()) {
        if (digits < 7) {
          fraction = fraction * 10 + text[pos].digitValue();
          ++digits;
        }
        ++pos;
      }
      if (digits == 0) return std::nullopt;
      for (; digits < 7; ++digits)
        fraction *= 10;
    }

    std::int64_t offsetSeconds = 0;
    if (pos < text.size() && text[pos] == QLatin1Char('Z')) {
      ++pos;
    } else if (pos < text.size() &&
               (text[pos] == QLatin1Char('+') || text[pos] == QLatin1Char('-'))) {
      const bool negative = text[pos] == QLatin1Char('-');
      ++pos;
      int offsetHours = 0, offsetMinutes = 0;
      if (!readDigits(text, pos, 2, offsetHours) || !expect(text, pos, ':') ||
          !readDigits(text, pos, 2, offsetMinutes))
        return std::nullopt;
      offsetSeconds = (offsetHours * 3600 + offsetMinutes * 60) * (negative ? -1 : 1);
    } else {
      // The server always writes UTC; a timestamp without a zone is not
      // guessed at.
      return std::nullopt;
    }
    if (pos != text.size()) return std::nullopt;

    const std::int64_t days =
        daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const std::int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second - offsetSeconds;
    return core::ServerInstant::fromSinceOrigin(core::WallTicks(seconds * PerSecond + fraction));
  }

  QString formatServerInstant(core::ServerInstant instant) {
    const std::int64_t ticks = instant.sinceOrigin().count();
    std::int64_t seconds = ticks / PerSecond;
    std::int64_t fraction = ticks % PerSecond;
    if (fraction < 0) {
      fraction += PerSecond;
      --seconds;
    }
    std::int64_t days = seconds / 86400;
    std::int64_t secondOfDay = seconds % 86400;
    if (secondOfDay < 0) {
      secondOfDay += 86400;
      --days;
    }
    const Civil date = civilFromDays(days);
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02uT%02lld:%02lld:%02lld.%07lldZ",
                  static_cast<long long>(date.year), date.month, date.day,
                  static_cast<long long>(secondOfDay / 3600),
                  static_cast<long long>(secondOfDay % 3600 / 60),
                  static_cast<long long>(secondOfDay % 60), static_cast<long long>(fraction));
    return QString::fromLatin1(buffer);
  }

} // namespace jfd::api
