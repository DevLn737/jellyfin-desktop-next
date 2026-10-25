#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jfd::diagnostics {

struct ZipEntry
{
  // Forward slashes, no leading slash.
  std::string name;
  std::string data;
};

// A zip archive with the entries stored without compression: every unzip
// tool reads it, and the sizes are known up front (the report caps them).
// dosDate and dosTime are the MS-DOS stamps the format uses.
std::string makeZip(const std::vector<ZipEntry>& entries, std::uint16_t dosDate, std::uint16_t dosTime);

// The zip format's CRC-32.
std::uint32_t crc32(const std::string& data);

}
