#include <jfd/offline/MatroskaAudio.h>

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QSet>
#include <QtEndian>

#include <cstdint>
#include <cstring>
#include <optional>

namespace jfd::offline {

  namespace {
    constexpr std::uint32_t IdEbml = 0x1A45DFA3;
    constexpr std::uint32_t IdSegment = 0x18538067;
    constexpr std::uint32_t IdSeekHead = 0x114D9B74;
    constexpr std::uint32_t IdSeek = 0x4DBB;
    constexpr std::uint32_t IdSeekId = 0x53AB;
    constexpr std::uint32_t IdSeekPosition = 0x53AC;
    constexpr std::uint32_t IdInfo = 0x1549A966;
    constexpr std::uint32_t IdDuration = 0x4489;
    constexpr std::uint32_t IdTracks = 0x1654AE6B;
    constexpr std::uint32_t IdTrackEntry = 0xAE;
    constexpr std::uint32_t IdTrackNumber = 0xD7;
    constexpr std::uint32_t IdTrackType = 0x83;
    constexpr std::uint32_t IdLanguage = 0x22B59C;
    constexpr std::uint32_t IdLanguageBcp47 = 0x22B59D;
    constexpr std::uint32_t IdCluster = 0x1F43B675;
    constexpr std::uint32_t IdTimestamp = 0xE7;
    constexpr std::uint32_t IdSimpleBlock = 0xA3;
    constexpr std::uint32_t IdBlockGroup = 0xA0;
    constexpr std::uint32_t IdBlock = 0xA1;
    constexpr std::uint32_t IdCues = 0x1C53BB6B;
    constexpr std::uint32_t IdCuePoint = 0xBB;
    constexpr std::uint32_t IdCueTime = 0xB3;
    constexpr std::uint32_t IdCueTrackPositions = 0xB7;
    constexpr std::uint32_t IdCueTrack = 0xF7;
    constexpr std::uint32_t IdCueClusterPosition = 0xF1;
    constexpr std::uint32_t IdVoid = 0xEC;
    constexpr std::uint32_t IdChapters = 0x1043A770;
    constexpr std::uint32_t IdTags = 0x1254C367;
    constexpr std::uint32_t IdAttachments = 0x1941A469;

    constexpr std::uint64_t Unknown = ~std::uint64_t(0);
    // Room kept after the segment start for the SeekHead written at the end.
    constexpr int SeekHeadRoom = 96;

    bool isTopLevel(std::uint32_t id) {
      switch (id) {
      case IdSeekHead: case IdInfo: case IdTracks: case IdCluster: case IdCues: case IdChapters:
      case IdTags: case IdAttachments: case IdSegment: case IdEbml:
        return true;
      default:
        return false;
      }
    }

    // --- Writing ---------------------------------------------------------

    QByteArray encodeId(std::uint32_t id) {
      QByteArray out;
      for (int shift = 24; shift >= 0; shift -= 8)
        if (const auto byte = static_cast<char>((id >> shift) & 0xff); byte != 0 || !out.isEmpty())
          out.append(byte);
      return out;
    }

    QByteArray encodeSize(std::uint64_t size, int width = 0) {
      if (width == 0) {
        width = 1;
        while (width < 8 && size >= (std::uint64_t(1) << (7 * width)) - 1) ++width;
      }
      QByteArray out(width, '\0');
      std::uint64_t value = size | (std::uint64_t(1) << (7 * width));
      for (int i = width - 1; i >= 0; --i) {
        out[i] = static_cast<char>(value & 0xff);
        value >>= 8;
      }
      return out;
    }

    QByteArray element(std::uint32_t id, const QByteArray& body) {
      return encodeId(id) + encodeSize(std::uint64_t(body.size())) + body;
    }

    QByteArray uintBody(std::uint64_t value) {
      QByteArray out;
      do {
        out.prepend(static_cast<char>(value & 0xff));
        value >>= 8;
      } while (value != 0);
      return out;
    }

    QByteArray voidElement(int totalSize) {
      // ID (1 byte) + an 8-byte size + the rest zeros.
      const int body = totalSize - 9;
      return encodeId(IdVoid) + encodeSize(std::uint64_t(body), 8) + QByteArray(body, '\0');
    }

    QByteArray floatBody(double value) {
      QByteArray out(8, '\0');
      std::uint64_t bits = 0;
      std::memcpy(&bits, &value, sizeof bits);
      qToBigEndian(bits, out.data());
      return out;
    }

    // --- Reading ---------------------------------------------------------

    class Reader {
    public:
      explicit Reader(QFile& file) : m_file(file) {}

      qint64 pos() const { return m_file.pos(); }
      bool atEnd() const { return m_file.atEnd(); }

      std::optional<std::uint32_t> readId() {
        unsigned char first = 0;
        if (!getByte(first) || first == 0) return std::nullopt;
        int length = 1;
        while (length <= 4 && !(first & (0x80 >> (length - 1)))) ++length;
        if (length > 4) return std::nullopt;
        std::uint32_t id = first;
        for (int i = 1; i < length; ++i) {
          unsigned char b = 0;
          if (!getByte(b)) return std::nullopt;
          id = (id << 8) | b;
        }
        return id;
      }

      // Unknown for an all-ones size.
      std::optional<std::uint64_t> readSize() {
        unsigned char first = 0;
        if (!getByte(first) || first == 0) return std::nullopt;
        int length = 1;
        while (!(first & (0x80 >> (length - 1)))) ++length;
        std::uint64_t value = first & (0xff >> length);
        bool allOnes = value == (std::uint64_t(0xff) >> length);
        for (int i = 1; i < length; ++i) {
          unsigned char b = 0;
          if (!getByte(b)) return std::nullopt;
          value = (value << 8) | b;
          allOnes = allOnes && b == 0xff;
        }
        return allOnes ? Unknown : value;
      }

      std::optional<QByteArray> read(std::uint64_t size) {
        if (size > std::uint64_t(256) * 1024 * 1024) return std::nullopt;
        QByteArray data = m_file.read(qint64(size));
        if (data.size() != qint64(size)) return std::nullopt;
        return data;
      }

      bool skip(std::uint64_t size) {
        if (size == Unknown) return false;
        return m_file.seek(m_file.pos() + qint64(size)) && m_file.pos() <= m_file.size();
      }

      void seek(qint64 to) { m_file.seek(to); }

    private:
      bool getByte(unsigned char& b) {
        char c = 0;
        if (!m_file.getChar(&c)) return false;
        b = static_cast<unsigned char>(c);
        return true;
      }

      QFile& m_file;
    };

    // Children of an element body held in memory.
    struct Child {
      std::uint32_t id = 0;
      QByteArray body;
      QByteArray raw;
    };

    std::optional<QList<Child>> children(const QByteArray& body) {
      QList<Child> out;
      qsizetype at = 0;
      const auto byte = [&](qsizetype i) { return static_cast<unsigned char>(body.at(i)); };
      while (at < body.size()) {
        const qsizetype start = at;
        const unsigned char first = byte(at);
        int idLength = 1;
        while (idLength <= 4 && !(first & (0x80 >> (idLength - 1)))) ++idLength;
        if (first == 0 || idLength > 4 || at + idLength > body.size()) return std::nullopt;
        std::uint32_t id = 0;
        for (int i = 0; i < idLength; ++i) id = (id << 8) | byte(at + i);
        at += idLength;
        if (at >= body.size()) return std::nullopt;
        const unsigned char sizeFirst = byte(at);
        int sizeLength = 1;
        while (sizeLength <= 8 && !(sizeFirst & (0x80 >> (sizeLength - 1)))) ++sizeLength;
        if (sizeFirst == 0 || sizeLength > 8 || at + sizeLength > body.size()) return std::nullopt;
        std::uint64_t size = sizeFirst & (0xff >> sizeLength);
        for (int i = 1; i < sizeLength; ++i) size = (size << 8) | byte(at + i);
        at += sizeLength;
        if (size > std::uint64_t(body.size() - at)) return std::nullopt;
        out.append({id, body.mid(at, qsizetype(size)), body.mid(start, at + qsizetype(size) - start)});
        at += qsizetype(size);
      }
      return out;
    }

    std::uint64_t uintOf(const QByteArray& body) {
      std::uint64_t v = 0;
      for (const char c : body) v = (v << 8) | static_cast<unsigned char>(c);
      return v;
    }

    // The track number at the start of a (Simple)Block.
    std::optional<std::uint64_t> blockTrack(const QByteArray& block, int* headerLength = nullptr) {
      if (block.isEmpty()) return std::nullopt;
      const auto first = static_cast<unsigned char>(block.at(0));
      int length = 1;
      while (length <= 8 && !(first & (0x80 >> (length - 1)))) ++length;
      if (first == 0 || length > 8 || block.size() < length + 3) return std::nullopt;
      std::uint64_t value = first & (0xff >> length);
      for (int i = 1; i < length; ++i) value = (value << 8) | static_cast<unsigned char>(block.at(i));
      if (headerLength) *headerLength = length;
      return value;
    }

    std::int16_t blockOffset(const QByteArray& block, int trackLength) {
      return qFromBigEndian<std::int16_t>(block.constData() + trackLength);
    }

  } // namespace

  bool extractMatroskaAudio(const QString& input, const QString& output, const QString& language,
                            QString* error) {
    QFile out(output);
    const auto fail = [&](const QString& why) {
      // Closed first: Windows does not delete an open file.
      out.close();
      if (error) *error = why;
      QFile::remove(output);
      return false;
    };
    QFile in(input);
    if (!in.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open the input"));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
      return fail(QStringLiteral("cannot write the output"));
    Reader reader(in);
    const auto write = [&](const QByteArray& data) { return out.write(data) == data.size(); };

    // EBML header, as it is.
    const qint64 headerStart = reader.pos();
    if (reader.readId() != IdEbml) return fail(QStringLiteral("not a Matroska file"));
    const auto headerSize = reader.readSize();
    if (!headerSize || *headerSize == Unknown || !reader.skip(*headerSize))
      return fail(QStringLiteral("broken EBML header"));
    const qint64 headerEnd = reader.pos();
    reader.seek(headerStart);
    const auto header = reader.read(std::uint64_t(headerEnd - headerStart));
    if (!header || !write(*header)) return fail(QStringLiteral("cannot write the output"));

    if (reader.readId() != IdSegment) return fail(QStringLiteral("no segment"));
    const auto segmentSize = reader.readSize();
    if (!segmentSize) return fail(QStringLiteral("broken segment"));
    const qint64 segmentEnd =
        *segmentSize == Unknown ? in.size() : std::min<qint64>(in.size(), reader.pos() + qint64(*segmentSize));

    // The output segment: size patched at the end, room for the SeekHead.
    const qint64 segmentSizeAt = out.pos() + 4;
    if (!write(encodeId(IdSegment) + encodeSize(0, 8))) return fail(QStringLiteral("write failed"));
    const qint64 segmentData = out.pos();
    if (!write(voidElement(SeekHeadRoom))) return fail(QStringLiteral("write failed"));

    qint64 infoAt = -1, tracksAt = -1, durationAt = -1;
    QSet<std::uint64_t> audioTracks;
    bool tracksSeen = false;
    struct Cue {
      std::uint64_t time;
      qint64 position;
    };
    QList<Cue> cues;
    std::int64_t lastTime = 0;
    std::uint64_t cueTrack = 0;

    std::optional<std::uint32_t> pending;
    std::optional<std::uint64_t> pendingSize;
    while (reader.pos() < segmentEnd) {
      std::optional<std::uint32_t> id = pending ? pending : reader.readId();
      std::optional<std::uint64_t> size = pending ? pendingSize : reader.readSize();
      pending.reset();
      if (!id || !size) break; // a cut tail: keep what was read
      if (*id == IdInfo || *id == IdTracks) {
        if (*size == Unknown) return fail(QStringLiteral("unknown-size header element"));
        const auto body = reader.read(*size);
        if (!body) break;
        const auto kids = children(*body);
        if (!kids) return fail(QStringLiteral("broken header element"));
        if (*id == IdInfo) {
          if (infoAt >= 0) continue;
          QByteArray rebuilt;
          for (const Child& c : *kids) {
            if (c.id == IdDuration) continue;
            rebuilt += c.raw;
          }
          infoAt = out.pos();
          const QByteArray duration = element(IdDuration, floatBody(0));
          const QByteArray info = element(IdInfo, rebuilt + duration);
          durationAt = infoAt + info.size() - 8;
          if (!write(info)) return fail(QStringLiteral("write failed"));
        } else {
          if (tracksSeen) continue;
          tracksSeen = true;
          QByteArray kept;
          for (const Child& entry : *kids) {
            if (entry.id != IdTrackEntry) continue;
            const auto fields = children(entry.body);
            if (!fields) return fail(QStringLiteral("broken track entry"));
            std::uint64_t number = 0, type = 0;
            for (const Child& f : *fields) {
              if (f.id == IdTrackNumber) number = uintOf(f.body);
              if (f.id == IdTrackType) type = uintOf(f.body);
            }
            if (type != 2 || number == 0) continue;
            audioTracks.insert(number);
            if (cueTrack == 0) cueTrack = number;
            QByteArray rebuilt;
            for (const Child& f : *fields)
              if (language.isEmpty() || (f.id != IdLanguage && f.id != IdLanguageBcp47)) rebuilt += f.raw;
            if (!language.isEmpty()) rebuilt += element(IdLanguage, language.toLatin1());
            kept += element(IdTrackEntry, rebuilt);
          }
          if (audioTracks.isEmpty()) return fail(QStringLiteral("no audio track"));
          tracksAt = out.pos();
          if (!write(element(IdTracks, kept))) return fail(QStringLiteral("write failed"));
        }
        continue;
      }
      if (*id == IdCluster) {
        if (!tracksSeen) return fail(QStringLiteral("a cluster before the tracks"));
        const qint64 clusterEnd = *size == Unknown ? segmentEnd : reader.pos() + qint64(*size);
        QByteArray body;
        std::uint64_t clusterTime = 0;
        bool hasBlock = false;
        bool broken = false;
        while (reader.pos() < clusterEnd) {
          const auto childId = reader.readId();
          const auto childSize = reader.readSize();
          if (!childId || !childSize) {
            broken = true;
            break;
          }
          if (*size == Unknown && isTopLevel(*childId)) {
            pending = childId;
            pendingSize = childSize;
            break;
          }
          if (*childSize == Unknown) {
            broken = true;
            break;
          }
          if (*childId == IdTimestamp) {
            const auto data = reader.read(*childSize);
            if (!data) { broken = true; break; }
            clusterTime = uintOf(*data);
            body += element(IdTimestamp, *data);
          } else if (*childId == IdSimpleBlock || *childId == IdBlockGroup) {
            const auto data = reader.read(*childSize);
            if (!data) { broken = true; break; }
            QByteArray block = *data;
            if (*childId == IdBlockGroup) {
              const auto parts = children(*data);
              if (!parts) continue;
              block.clear();
              for (const Child& p : *parts)
                if (p.id == IdBlock) block = p.body;
            }
            int trackLength = 0;
            const auto track = blockTrack(block, &trackLength);
            if (!track || !audioTracks.contains(*track)) continue;
            lastTime = std::max<std::int64_t>(lastTime, std::int64_t(clusterTime) + blockOffset(block, trackLength));
            body += element(*childId, *data);
            hasBlock = true;
          } else if (!reader.skip(*childSize)) {
            broken = true;
            break;
          }
        }
        if (hasBlock) {
          cues.append({clusterTime, out.pos() - segmentData});
          if (!write(element(IdCluster, body))) return fail(QStringLiteral("write failed"));
        }
        if (broken) break;
        continue;
      }
      // Everything else (SeekHead, Cues, Tags, Chapters, Attachments, Void)
      // is left out.
      if (*size == Unknown || !reader.skip(*size)) break;
    }
    if (!tracksSeen || infoAt < 0) return fail(QStringLiteral("no tracks or segment info"));
    if (cues.isEmpty()) return fail(QStringLiteral("no audio blocks"));

    // Cues: one point per cluster.
    QByteArray points;
    for (const Cue& cue : std::as_const(cues)) {
      const QByteArray position = element(IdCueTrack, uintBody(cueTrack)) +
                                  element(IdCueClusterPosition, uintBody(std::uint64_t(cue.position)));
      points += element(IdCuePoint, element(IdCueTime, uintBody(cue.time)) +
                                       element(IdCueTrackPositions, position));
    }
    const qint64 cuesAt = out.pos();
    if (!write(element(IdCues, points))) return fail(QStringLiteral("write failed"));
    const qint64 end = out.pos();

    // Patches: segment size, duration, SeekHead in the room kept for it.
    const auto seek = [&](std::uint32_t target, qint64 at) {
      return element(IdSeek, element(IdSeekId, encodeId(target)) +
                                 element(IdSeekPosition, uintBody(std::uint64_t(at - segmentData))));
    };
    const QByteArray seekHead =
        element(IdSeekHead, seek(IdInfo, infoAt) + seek(IdTracks, tracksAt) + seek(IdCues, cuesAt));
    if (seekHead.size() + 9 > SeekHeadRoom) return fail(QStringLiteral("seek head too large"));
    const QByteArray room = seekHead + voidElement(SeekHeadRoom - int(seekHead.size()));
    const double duration = double(lastTime) + 1.0;
    bool ok = out.seek(segmentSizeAt) && write(encodeSize(std::uint64_t(end - segmentData), 8)) &&
              out.seek(segmentData) && write(room) && out.seek(durationAt) && write(floatBody(duration));
    ok = ok && out.flush();
    out.close();
    if (!ok) return fail(QStringLiteral("write failed"));
    return true;
  }

} // namespace jfd::offline
