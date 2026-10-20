#include <jfd/api/ServerVersion.h>

namespace jfd::api {

  std::optional<ServerVersion> ServerVersion::parse(QStringView text) {
    int parts[3] = {0, 0, 0};
    int count = 0;
    qsizetype pos = 0;
    while (count < 3) {
      const qsizetype start = pos;
      long long value = 0;
      while (pos < text.size() && text[pos].isDigit()) {
        value = value * 10 + text[pos].digitValue();
        if (value > 1'000'000) return std::nullopt;
        ++pos;
      }
      if (pos == start) return std::nullopt;
      parts[count++] = static_cast<int>(value);
      if (pos < text.size() && text[pos] == QLatin1Char('.') && count < 3) {
        ++pos;
        continue;
      }
      break;
    }
    // "12.2" is accepted; a bare "12" is not a version.
    if (count < 2) return std::nullopt;
    return ServerVersion{parts[0], parts[1], parts[2]};
  }

  QString ServerVersion::toString() const {
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
  }

} // namespace jfd::api
