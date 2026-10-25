#include <jfd/diagnostics/Zip.h>

#include <array>

namespace jfd::diagnostics {

namespace {

const std::array<std::uint32_t, 256>& crcTable()
{
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k)
        c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  return table;
}

void put16(std::string& out, std::uint16_t v)
{
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>(v >> 8));
}

void put32(std::string& out, std::uint32_t v)
{
  put16(out, static_cast<std::uint16_t>(v & 0xFFFF));
  put16(out, static_cast<std::uint16_t>(v >> 16));
}

// Bit 11: the names are UTF-8.
constexpr std::uint16_t Utf8Names = 0x0800;
constexpr std::uint16_t Version = 20;

}

std::uint32_t crc32(const std::string& data)
{
  const auto& table = crcTable();
  std::uint32_t c = 0xFFFFFFFFu;
  for (const char ch : data)
    c = table[(c ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

std::string makeZip(const std::vector<ZipEntry>& entries, std::uint16_t dosDate, std::uint16_t dosTime)
{
  std::string out;
  std::string directory;
  for (const ZipEntry& entry : entries) {
    const std::uint32_t crc = crc32(entry.data);
    const auto size = static_cast<std::uint32_t>(entry.data.size());
    const auto offset = static_cast<std::uint32_t>(out.size());
    const auto nameLength = static_cast<std::uint16_t>(entry.name.size());

    put32(out, 0x04034B50u);
    put16(out, Version);
    put16(out, Utf8Names);
    put16(out, 0); // stored
    put16(out, dosTime);
    put16(out, dosDate);
    put32(out, crc);
    put32(out, size);
    put32(out, size);
    put16(out, nameLength);
    put16(out, 0);
    out += entry.name;
    out += entry.data;

    put32(directory, 0x02014B50u);
    put16(directory, Version);
    put16(directory, Version);
    put16(directory, Utf8Names);
    put16(directory, 0);
    put16(directory, dosTime);
    put16(directory, dosDate);
    put32(directory, crc);
    put32(directory, size);
    put32(directory, size);
    put16(directory, nameLength);
    put16(directory, 0);
    put16(directory, 0);
    put16(directory, 0);
    put16(directory, 0);
    put32(directory, 0);
    put32(directory, offset);
    directory += entry.name;
  }
  const auto directoryOffset = static_cast<std::uint32_t>(out.size());
  out += directory;
  put32(out, 0x06054B50u);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<std::uint16_t>(entries.size()));
  put16(out, static_cast<std::uint16_t>(entries.size()));
  put32(out, static_cast<std::uint32_t>(directory.size()));
  put32(out, directoryOffset);
  put16(out, 0);
  return out;
}

}
