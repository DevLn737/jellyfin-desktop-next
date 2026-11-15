#include <jfd/library/LiveTvGuide.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ErrorText.h>

#include <algorithm>
#include <cmath>

namespace jfd::library {

  namespace {
    constexpr int Slot = 30;
    // Programmes listed beyond each side of the view.
    constexpr int Margin = 60;

    int floorTo(int value, int step) {
      return value >= 0 ? value / step * step : -((-value + step - 1) / step) * step;
    }
  } // namespace

  LiveTvGuide::LiveTvGuide(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_api(client), m_options(std::move(options)) {
    m_options.hours = std::max(1, m_options.hours);
    m_options.chunkHours = std::max(1, m_options.chunkHours);
    m_tick.setInterval(m_options.tickMs);
    connect(&m_tick, &QTimer::timeout, this, &LiveTvGuide::tick);
    m_window = window();
  }

  QDateTime LiveTvGuide::now() const {
    return m_options.now ? m_options.now().toUTC() : QDateTime::currentDateTimeUtc();
  }

  double LiveTvGuide::minutesOf(const QDateTime& when) const {
    if (!m_rangeStart.isValid() || !when.isValid()) return 0.0;
    return static_cast<double>(m_rangeStart.msecsTo(when)) / 60'000.0;
  }

  double LiveTvGuide::nowMinutes() const { return minutesOf(now()); }

  int LiveTvGuide::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QList<api::LiveTvProgram> LiveTvGuide::programsAt(int row) const {
    if (row < 0 || row >= count()) return {};
    return m_rows[row].programs;
  }

  std::optional<api::LiveTvProgram> LiveTvGuide::programAt(int row, int minutes) const {
    if (row < 0 || row >= count()) return std::nullopt;
    const QDateTime at = m_rangeStart.addSecs(qint64(minutes) * 60);
    for (const api::LiveTvProgram& p : m_rows[row].programs)
      if (p.isOnAt(at)) return p;
    return std::nullopt;
  }

  QVariant LiveTvGuide::data(const QModelIndex& index, int role) const {
    if (index.parent().isValid() || index.row() < 0 || index.row() >= count()) return {};
    const Row& r = m_rows[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
      return r.channel.name;
    case IdRole:
      return r.channel.id;
    case NumberRole:
      return r.channel.number;
    case ImageUrlRole:
      return r.imageUrl;
    case ProgramsRole: {
      QVariantList cells;
      const auto [from, to] = m_window;
      for (const api::LiveTvProgram& p : r.programs) {
        const double start = minutesOf(p.start);
        const double end = minutesOf(p.end);
        if (end <= from || start >= to) continue;
        cells.append(QVariantMap{{QStringLiteral("id"), p.id},
                                 {QStringLiteral("title"), p.name},
                                 {QStringLiteral("offset"), start},
                                 {QStringLiteral("length"), end - start}});
      }
      return cells;
    }
    default:
      return {};
    }
  }

  QHash<int, QByteArray> LiveTvGuide::roleNames() const {
    return {{IdRole, "channelId"},
            {NameRole, "name"},
            {NumberRole, "number"},
            {ImageUrlRole, "imageUrl"},
            {ProgramsRole, "programs"}};
  }

  void LiveTvGuide::reload() {
    const quint64 serial = ++m_serial;
    const QString keepChannel = m_focusChannel;
    setLoading(true);
    setError({});

    // The axis starts at the half hour before now.
    QDateTime start = now();
    const qint64 slotSecs = qint64(Slot) * 60;
    start = QDateTime::fromMSecsSinceEpoch(start.toSecsSinceEpoch() / slotSecs * slotSecs * 1000).toUTC();
    const bool rangeMoved = start != m_rangeStart;
    m_rangeStart = start;

    beginResetModel();
    m_rows.clear();
    m_rowOf.clear();
    endResetModel();
    m_chunks = QList<Chunk>((m_options.hours + m_options.chunkHours - 1) / m_options.chunkHours, Chunk::Missing);
    m_pendingChunks = 0;
    if (rangeMoved) Q_EMIT rangeChanged();
    Q_EMIT nowChanged();

    api::LiveTvChannelQuery query;
    query.limit = m_options.channelPageSize;
    query.addCurrentProgram = false;
    m_api.allChannels(query, this, [this, serial, keepChannel](api::Result<QList<api::LiveTvChannel>> channels) {
      if (serial != m_serial) return;
      if (!channels) return fail(channels.error());
      const int pixels = static_cast<int>(
          std::lround(m_options.imageWidth * std::max<qreal>(1.0, m_options.devicePixelRatio)));
      QList<Row> rows;
      rows.reserve(channels->size());
      for (const api::LiveTvChannel& c : *channels) {
        Row r;
        if (!c.primaryImageTag.isEmpty()) {
          api::ImageRequest image;
          image.itemId = c.id;
          image.tag = c.primaryImageTag;
          image.maxWidth = pixels;
          r.imageUrl = m_client.imageUrl(image);
        }
        r.channel = c;
        rows.append(std::move(r));
      }
      const bool countChanges = rows.size() != m_rows.size();
      beginResetModel();
      m_rows = std::move(rows);
      for (int i = 0; i < count(); ++i) m_rowOf.insert(m_rows[i].channel.id, i);
      endResetModel();
      if (countChanges) Q_EMIT countChanged();

      // The focus comes back to its channel, at now.
      const int row = m_rowOf.value(keepChannel, 0);
      m_viewStart = 0;
      m_window = window();
      Q_EMIT viewChanged();
      setFocus(row, std::clamp(static_cast<int>(nowMinutes()), 0, rangeMinutes() - 1), false);
      if (m_rows.isEmpty()) {
        m_tick.stop();
        setLoading(false);
        return;
      }
      m_tick.start();
      loadChunks(m_window.first, m_window.second);
      if (m_pendingChunks == 0) setLoading(false);
    });
  }

  void LiveTvGuide::loadChunks(int fromMinutes, int toMinutes) {
    if (m_rows.isEmpty()) return;
    const int chunkMinutes = m_options.chunkHours * 60;
    const int first = std::max(0, fromMinutes) / chunkMinutes;
    const int last = (std::min(rangeMinutes(), toMinutes) - 1) / chunkMinutes;
    for (int chunk = first; chunk <= last && chunk < m_chunks.size(); ++chunk)
      if (m_chunks[chunk] == Chunk::Missing) loadChunk(chunk);
  }

  void LiveTvGuide::loadChunk(int chunk) {
    m_chunks[chunk] = Chunk::Loading;
    ++m_pendingChunks;
    setLoading(true);
    ++m_chunksRequested;
    const quint64 serial = m_serial;
    const qint64 chunkSecs = qint64(m_options.chunkHours) * 3600;
    api::LiveTvProgramQuery query;
    query.from = m_rangeStart.addSecs(chunk * chunkSecs);
    query.to = std::min(query.from.addSecs(chunkSecs), m_rangeStart.addSecs(qint64(rangeMinutes()) * 60));
    query.limit = m_options.programPageSize;
    m_api.allPrograms(query, this, [this, serial, chunk](api::Result<QList<api::LiveTvProgram>> programs) {
      if (serial != m_serial) return;
      --m_pendingChunks;
      if (!programs) {
        // Tried again when the view comes back to it; the channels stay.
        m_chunks[chunk] = Chunk::Missing;
        if (m_pendingChunks == 0) setLoading(false);
        return;
      }
      m_chunks[chunk] = Chunk::Loaded;
      addPrograms(*programs);
      if (m_pendingChunks == 0) setLoading(false);
    });
  }

  void LiveTvGuide::addPrograms(const QList<api::LiveTvProgram>& programs) {
    QList<int> touched;
    for (const api::LiveTvProgram& p : programs) {
      const auto at = m_rowOf.constFind(p.channelId);
      if (at == m_rowOf.cend()) continue;
      QList<api::LiveTvProgram>& list = m_rows[*at].programs;
      // A programme across a chunk's edge comes with both chunks.
      if (std::any_of(list.cbegin(), list.cend(), [&](const api::LiveTvProgram& q) { return q.id == p.id; }))
        continue;
      const auto place = std::upper_bound(list.begin(), list.end(), p,
                                          [](const api::LiveTvProgram& a, const api::LiveTvProgram& b) {
                                            return a.start < b.start;
                                          });
      list.insert(place, p);
      if (!touched.contains(*at)) touched.append(*at);
    }
    for (int row : std::as_const(touched)) Q_EMIT dataChanged(index(row), index(row), {ProgramsRole});
    if (touched.contains(m_focusRow)) Q_EMIT focusChanged();
  }

  std::pair<int, int> LiveTvGuide::window() const {
    const int from = floorTo(m_viewStart - Margin, Slot);
    const int to = floorTo(m_viewStart + m_viewMinutes + Margin + Slot - 1, Slot);
    return {from, to};
  }

  void LiveTvGuide::setViewStartMinutes(int minutes) {
    minutes = std::clamp(minutes, 0, std::max(0, rangeMinutes() - m_viewMinutes));
    if (minutes == m_viewStart) return;
    m_viewStart = minutes;
    viewMoved();
  }

  void LiveTvGuide::setViewMinutes(int minutes) {
    minutes = std::clamp(minutes, Slot, rangeMinutes());
    if (minutes == m_viewMinutes) return;
    m_viewMinutes = minutes;
    m_viewStart = std::clamp(m_viewStart, 0, std::max(0, rangeMinutes() - m_viewMinutes));
    viewMoved();
  }

  void LiveTvGuide::viewMoved() {
    Q_EMIT viewChanged();
    const auto next = window();
    if (next != m_window) {
      m_window = next;
      if (!m_rows.isEmpty()) Q_EMIT dataChanged(index(0), index(count() - 1), {ProgramsRole});
    }
    loadChunks(m_window.first, m_window.second);
  }

  void LiveTvGuide::followFocus() {
    // The focused time stays at least a slot inside the view.
    int start = m_viewStart;
    if (m_focusMinutes < m_viewStart) {
      start = floorTo(m_focusMinutes, Slot);
    } else if (m_focusMinutes >= m_viewStart + m_viewMinutes - Slot) {
      start = floorTo(m_focusMinutes, Slot) - m_viewMinutes + 2 * Slot;
    }
    setViewStartMinutes(start);
  }

  void LiveTvGuide::setFocus(int row, int minutes, bool snapToStart) {
    if (m_rows.isEmpty()) {
      m_focusRow = 0;
      m_focusMinutes = std::clamp(minutes, 0, rangeMinutes() - 1);
      m_focusChannel.clear();
      Q_EMIT focusChanged();
      return;
    }
    m_focusRow = std::clamp(row, 0, count() - 1);
    m_focusMinutes = std::clamp(minutes, 0, rangeMinutes() - 1);
    m_focusChannel = m_rows[m_focusRow].channel.id;
    if (snapToStart) {
      // The first whole minute inside the programme: one that begins at
      // 16:30:30 snaps to 16:31, which still belongs to it.
      if (const auto p = programAt(m_focusRow, m_focusMinutes)) {
        const int first = static_cast<int>(std::ceil(minutesOf(p->start)));
        if (first < minutesOf(p->end) && first <= m_focusMinutes)
          m_focusMinutes = std::clamp(first, 0, rangeMinutes() - 1);
      }
    }
    followFocus();
    loadChunks(m_focusMinutes, m_focusMinutes + 1);
    Q_EMIT focusChanged();
  }

  bool LiveTvGuide::moveFocus(int dx, int dy) {
    if (m_rows.isEmpty()) return false;
    if (dy != 0) {
      const int row = std::clamp(m_focusRow + dy, 0, count() - 1);
      if (row == m_focusRow) return false;
      setFocus(row, m_focusMinutes, false);
      return true;
    }
    if (dx == 0) return false;
    const auto current = programAt(m_focusRow, m_focusMinutes);
    int target = 0;
    if (dx > 0) {
      if (current) {
        target = static_cast<int>(std::ceil(minutesOf(current->end)));
      } else {
        // An empty slot: the next programme in the row, else the next slot.
        target = floorTo(m_focusMinutes, Slot) + Slot;
        for (const api::LiveTvProgram& p : m_rows[m_focusRow].programs) {
          const double start = minutesOf(p.start);
          if (start > m_focusMinutes) {
            target = std::min(target, static_cast<int>(std::ceil(start)));
            break;
          }
        }
      }
      if (target >= rangeMinutes()) return false;
    } else {
      const double start = current ? minutesOf(current->start) : m_focusMinutes;
      if (start <= 0) return false;
      if (current) {
        target = static_cast<int>(std::ceil(start)) - 1;
      } else {
        // An empty slot: the slot before, or the programme ending in it.
        target = floorTo(m_focusMinutes - 1, Slot);
        const QList<api::LiveTvProgram>& list = m_rows[m_focusRow].programs;
        for (auto p = list.crbegin(); p != list.crend(); ++p) {
          const double end = minutesOf(p->end);
          if (end <= m_focusMinutes) {
            if (end >= target) target = static_cast<int>(std::ceil(end)) - 1;
            break;
          }
        }
      }
    }
    setFocus(m_focusRow, target, true);
    return true;
  }

  bool LiveTvGuide::pageFocus(int direction) {
    if (m_rows.isEmpty() || direction == 0) return false;
    const int step = direction > 0 ? m_viewMinutes : -m_viewMinutes;
    const int target = std::clamp(m_focusMinutes + step, 0, rangeMinutes() - 1);
    if (target == m_focusMinutes) return false;
    // The view moves by the same screen, so the focus stays where it was on
    // it.
    setViewStartMinutes(m_viewStart + step);
    setFocus(m_focusRow, target, false);
    return true;
  }

  void LiveTvGuide::focusAt(int row, int minutes) { setFocus(row, minutes, false); }

  void LiveTvGuide::focusNow() {
    setFocus(m_focusRow, static_cast<int>(nowMinutes()), false);
  }

  QVariantMap LiveTvGuide::focusedProgram() const {
    if (m_focusRow < 0 || m_focusRow >= count()) return {};
    const auto p = programAt(m_focusRow, m_focusMinutes);
    if (!p) return {};
    const Row& r = m_rows[m_focusRow];
    const double start = minutesOf(p->start);
    const double end = minutesOf(p->end);
    return {{QStringLiteral("id"), p->id},
            {QStringLiteral("title"), p->name},
            {QStringLiteral("episodeTitle"), p->episodeTitle},
            {QStringLiteral("overview"), p->overview},
            {QStringLiteral("offset"), start},
            {QStringLiteral("length"), end - start},
            {QStringLiteral("start"), p->start.toLocalTime()},
            {QStringLiteral("end"), p->end.toLocalTime()},
            {QStringLiteral("channelId"), r.channel.id},
            {QStringLiteral("channelName"), r.channel.name}};
  }

  QString LiveTvGuide::focusedChannelId() const {
    return m_focusRow >= 0 && m_focusRow < count() ? m_rows[m_focusRow].channel.id : QString();
  }

  void LiveTvGuide::tick() {
    if (m_rows.isEmpty()) return;
    Q_EMIT nowChanged();
  }

  void LiveTvGuide::clear() {
    ++m_serial;
    m_tick.stop();
    const bool had = !m_rows.isEmpty();
    beginResetModel();
    m_rows.clear();
    m_rowOf.clear();
    endResetModel();
    m_chunks.clear();
    m_pendingChunks = 0;
    m_focusRow = 0;
    m_focusMinutes = 0;
    m_focusChannel.clear();
    if (had) Q_EMIT countChanged();
    Q_EMIT focusChanged();
    setLoading(false);
    setError({});
  }

  void LiveTvGuide::fail(const api::ApiError& error) {
    setLoading(false);
    setError(library::errorText(error));
  }

  void LiveTvGuide::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void LiveTvGuide::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

} // namespace jfd::library
