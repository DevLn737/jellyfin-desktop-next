#include <jfd/diagnostics/LogFile.h>

#include <system_error>

namespace jfd::diagnostics {

bool LogFile::open(const std::filesystem::path& current, const std::filesystem::path& previous)
{
  close();
  m_current = current;
  m_previous = previous;
  m_rotations = 0;
  return start(true);
}

bool LogFile::start(bool keepPrevious)
{
  std::error_code error;
  if (keepPrevious) {
    std::filesystem::remove(m_previous, error);
    std::filesystem::rename(m_current, m_previous, error);
  }
  // Text mode: the platform's line ends, as before.
  m_out.open(m_current, std::ios::out | std::ios::trunc);
  m_size = 0;
  m_dirty = false;
  m_lastFlush = Clock::now();
  return m_out.is_open();
}

void LogFile::write(std::string_view line, bool urgent, Clock::time_point now)
{
  if (!m_out.is_open())
    return;
  if (m_size > 0 && m_size + line.size() > m_options.maxBytes) {
    // Closed first: Windows cannot rename an open file.
    m_out.close();
    ++m_rotations;
    if (!start(true))
      return;
  }
  m_out.write(line.data(), static_cast<std::streamsize>(line.size()));
  m_size += line.size();
  if (!m_dirty) {
    // The interval counts from the oldest line not yet on disk.
    m_dirty = true;
    m_lastFlush = now;
  }
  if (urgent)
    flush();
  else
    flushIfDue(now);
}

void LogFile::flushIfDue(Clock::time_point now)
{
  if (m_dirty && now - m_lastFlush >= m_options.flushInterval)
    flush();
}

void LogFile::flush()
{
  if (!m_out.is_open())
    return;
  m_out.flush();
  m_dirty = false;
  m_lastFlush = Clock::now();
}

void LogFile::close()
{
  if (m_out.is_open()) {
    m_out.flush();
    m_out.close();
  }
  m_dirty = false;
  m_size = 0;
}

}
