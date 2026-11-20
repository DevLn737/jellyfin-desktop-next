#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "FakeFileServer.h"
#include "OfflineFixtures.h"

#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/LibraryNaming.h>
#include <jfd/offline/MatroskaAudio.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/OfflineLocalSource.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace jfd::offline;
using namespace jfd::test;

namespace {

  // --- A small Matroska writer for the extraction tests -----------------

  QByteArray id(std::uint32_t value) {
    QByteArray out;
    for (int shift = 24; shift >= 0; shift -= 8)
      if (const auto b = char((value >> shift) & 0xff); b != 0 || !out.isEmpty()) out.append(b);
    return out;
  }

  QByteArray size(std::uint64_t value, bool unknown = false) {
    if (unknown) return QByteArray::fromHex("01ffffffffffffff");
    QByteArray out(8, '\0');
    value |= std::uint64_t(1) << 56;
    for (int i = 7; i >= 0; --i) {
      out[i] = char(value & 0xff);
      value >>= 8;
    }
    return out;
  }

  QByteArray el(std::uint32_t i, const QByteArray& body) { return id(i) + size(std::uint64_t(body.size())) + body; }
  QByteArray uint8(std::uint32_t i, int value) { return el(i, QByteArray(1, char(value))); }

  QByteArray block(int track, int offset, const QByteArray& payload) {
    QByteArray b;
    b.append(char(0x80 | track));
    b.append(char((offset >> 8) & 0xff));
    b.append(char(offset & 0xff));
    b.append(char(0x80));
    return el(0xA3, b + payload);
  }

  // Track 1 video, track 2 audio; two clusters, the second of unknown size
  // (as a live writer leaves it), then Cues and Tags to be dropped.
  QByteArray sampleMatroska() {
    const QByteArray header = el(0x1A45DFA3, el(0x4282, "matroska") + uint8(0x4287, 4));
    const QByteArray info = el(0x1549A966, el(0x2AD7B1, QByteArray::fromHex("0f4240")) +
                                               el(0x4489, QByteArray::fromHex("0000000000000000")));
    const QByteArray video = el(0xAE, uint8(0xD7, 1) + uint8(0x83, 1) + el(0x86, "V_MPEG4/ISO/AVC"));
    const QByteArray audio =
        el(0xAE, uint8(0xD7, 2) + uint8(0x83, 2) + el(0x86, "A_AC3") + el(0x22B59C, "und"));
    const QByteArray tracks = el(0x1654AE6B, video + audio);
    const QByteArray cluster1 = el(0x1F43B675, uint8(0xE7, 0) + block(1, 0, "VVVV") + block(2, 0, "aaaa") +
                                                   block(2, 32, "bbbb"));
    const QByteArray cluster2 = id(0x1F43B675) + size(0, true) + el(0xE7, QByteArray::fromHex("03e8")) +
                                block(1, 0, "WWWW") + block(2, 0, "cccc") + block(2, 32, "dddd");
    const QByteArray tail = el(0x1C53BB6B, QByteArray(4, 'x')) + el(0x1254C367, QByteArray(4, 'y'));
    const QByteArray segment = info + tracks + cluster1 + cluster2 + tail;
    return header + id(0x18538067) + size(0, true) + segment;
  }

  bool writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
  }

  QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }

  QJsonObject episode(const QString& itemId, const QJsonObject& source) {
    QJsonObject item = itemJson(itemId, source, QStringLiteral("Episode"));
    item.insert(QStringLiteral("Name"), QStringLiteral("The: Pilot?"));
    item.insert(QStringLiteral("SeriesName"), QStringLiteral("Alice/Bob Show"));
    item.insert(QStringLiteral("ParentIndexNumber"), 1);
    item.insert(QStringLiteral("IndexNumber"), 2);
    return item;
  }

  // An episode with an external Russian dub and an external forced subtitle,
  // as the server lists them (external streams first).
  QJsonObject episodeSource(const QString& itemId, qint64 bytes) {
    QJsonObject source = mediaSourceJson(itemId, bytes);
    source.insert(QStringLiteral("Path"), QStringLiteral("/media/Shows/Show/Season 01/Show S01E02.mkv"));
    source.insert(QStringLiteral("RunTimeTicks"), 10000000.0);
    source.insert(QStringLiteral("MediaStreams"),
                  QJsonArray{
                      QJsonObject{{QStringLiteral("Index"), 0},
                                  {QStringLiteral("Type"), QStringLiteral("Subtitle")},
                                  {QStringLiteral("Codec"), QStringLiteral("subrip")},
                                  {QStringLiteral("Language"), QStringLiteral("rus")},
                                  {QStringLiteral("IsForced"), true},
                                  {QStringLiteral("IsExternal"), true},
                                  {QStringLiteral("Path"),
                                   QStringLiteral("/media/Shows/Show/Season 01/Show S01E02.ru.forced.srt")}},
                      QJsonObject{{QStringLiteral("Index"), 1},
                                  {QStringLiteral("Type"), QStringLiteral("Audio")},
                                  {QStringLiteral("Codec"), QStringLiteral("ac3")},
                                  {QStringLiteral("Language"), QStringLiteral("rus")},
                                  {QStringLiteral("IsExternal"), true},
                                  {QStringLiteral("Path"),
                                   QStringLiteral("/media/Shows/Show/Season 01/Show S01E02.ru.mka")}},
                      QJsonObject{{QStringLiteral("Index"), 2},
                                  {QStringLiteral("Type"), QStringLiteral("Video")},
                                  {QStringLiteral("Codec"), QStringLiteral("h264")}},
                  });
    return source;
  }

} // namespace

TEST_CASE("Library names follow the Jellyfin and Kodi conventions and are safe on Windows") {
  QJsonObject item = episode(itemIdFor(1), {});
  CHECK(libraryBase(item) == QStringLiteral("Shows/Alice-Bob Show/Season 01/Alice-Bob Show - S01E02 - The - Pilot_"));
  item.insert(QStringLiteral("Name"), QStringLiteral("Alice-Bob Show S01E02"));
  item.insert(QStringLiteral("IndexNumberEnd"), 3);
  CHECK(libraryBase(item) == QStringLiteral("Shows/Alice-Bob Show/Season 01/Alice-Bob Show - S01E02-E03"));

  QJsonObject movie{{QStringLiteral("Type"), QStringLiteral("Movie")},
                    {QStringLiteral("Name"), QStringLiteral("Dune. ")},
                    {QStringLiteral("ProductionYear"), 2021}};
  CHECK(libraryBase(movie) == QStringLiteral("Movies/Dune (2021)/Dune (2021)"));

  QJsonObject track{{QStringLiteral("Type"), QStringLiteral("Audio")},
                    {QStringLiteral("Name"), QStringLiteral("Song")},
                    {QStringLiteral("Album"), QStringLiteral("Album")},
                    {QStringLiteral("AlbumArtist"), QStringLiteral("AC/DC")},
                    {QStringLiteral("IndexNumber"), 3}};
  CHECK(libraryBase(track) == QStringLiteral("Music/AC-DC/Album/03 - Song"));

  CHECK(safeFileName(QStringLiteral("CON")) == QStringLiteral("_CON"));
  CHECK(safeFileName(QStringLiteral("  ...  ")) == QStringLiteral("Untitled"));
  CHECK(safeFileName(QStringLiteral("a\tb<c>")) == QStringLiteral("ab_c_"));
}

TEST_CASE("Sidecars keep the server's suffix, or get one from the stream") {
  const QString media = QStringLiteral("/media/Show/Show S01E01.mkv");
  QJsonObject stream{{QStringLiteral("Path"), QStringLiteral("/media/Show/Show S01E01.ru.forced.srt")},
                     {QStringLiteral("Language"), QStringLiteral("rus")}};
  CHECK(sidecarSuffix(stream, media) == QStringLiteral("ru.forced"));
  // A Windows server's path.
  CHECK(sidecarSuffix(QJsonObject{{QStringLiteral("Path"), QStringLiteral("D:\\TV\\Ep.en.ass")}},
                      QStringLiteral("D:\\TV\\Ep.mkv")) == QStringLiteral("en"));
  // Not named after the video: from the stream.
  QJsonObject other{{QStringLiteral("Path"), QStringLiteral("/media/Show/Subs/rus.srt")},
                    {QStringLiteral("Language"), QStringLiteral("rus")},
                    {QStringLiteral("Title"), QStringLiteral("Signs.Songs")},
                    {QStringLiteral("IsForced"), true},
                    {QStringLiteral("IsHearingImpaired"), true}};
  CHECK(sidecarSuffix(other, media) == QStringLiteral("Signs Songs.rus.forced.sdh"));
  CHECK(sidecarSuffix(QJsonObject{{QStringLiteral("Language"), QStringLiteral("und")}}, media).isEmpty());

  CHECK(sidecarPath(QStringLiteral("A/B"), QStringLiteral("ru"), QStringLiteral("srt"), {}) ==
        QStringLiteral("A/B.ru.srt"));
  CHECK(sidecarPath(QStringLiteral("A/B"), QStringLiteral("ru"), QStringLiteral("srt"),
                    {QStringLiteral("A/B.ru.srt")}) == QStringLiteral("A/B.2.ru.srt"));
}

TEST_CASE("Matroska audio extraction keeps the audio blocks only and is seekable") {
  QTemporaryDir dir;
  const QString in = dir.filePath(QStringLiteral("in.mkv"));
  const QString out = dir.filePath(QStringLiteral("out.mka"));
  REQUIRE(writeFile(in, sampleMatroska()));
  QString error;
  REQUIRE(extractMatroskaAudio(in, out, QStringLiteral("rus"), &error));
  const QByteArray data = readFile(out);
  CHECK(data.contains("aaaa"));
  CHECK(data.contains("bbbb"));
  CHECK(data.contains("cccc"));
  CHECK(data.contains("dddd"));
  CHECK_FALSE(data.contains("VVVV"));
  CHECK_FALSE(data.contains("WWWW"));
  CHECK_FALSE(data.contains("V_MPEG4"));
  CHECK(data.contains("A_AC3"));
  CHECK(data.contains("rus"));
  CHECK_FALSE(data.contains("und"));
  // A SeekHead, Cues, no Tags of the input.
  CHECK(data.contains(QByteArray::fromHex("114d9b74")));
  CHECK(data.contains(QByteArray::fromHex("1c53bb6b")));
  CHECK_FALSE(data.contains("yyyy"));
  // The segment's size was patched to what follows it.
  const qsizetype segment = data.indexOf(QByteArray::fromHex("18538067"));
  REQUIRE(segment > 0);
  CHECK(static_cast<unsigned char>(data.at(segment + 4)) == 0x01);
  std::uint64_t value = 0;
  for (int i = 5; i < 12; ++i) value = (value << 8) | static_cast<unsigned char>(data.at(segment + i));
  CHECK(value == std::uint64_t(data.size() - (segment + 12)));

  // Not Matroska, or no audio: refused, nothing left behind.
  REQUIRE(writeFile(in, QByteArray("not a video")));
  CHECK_FALSE(extractMatroskaAudio(in, out, {}, &error));
  CHECK_FALSE(QFile::exists(out));
}

TEST_CASE("An episode downloads into Show/Season with its external audio and subtitles beside it") {
  FakeFileServer server;
  const QString itemId = itemIdFor(41);
  const QByteArray content = patternBytes(256 * 1024, 4);
  server.setFile(QStringLiteral("/Items/") + itemId + QStringLiteral("/Download"), {content});
  server.setFile(QStringLiteral("/Videos/") + itemId + QStringLiteral("/stream.mkv"), {sampleMatroska()});

  QTemporaryDir dir;
  DownloadStore store(dir.filePath(QStringLiteral("downloads")));
  OfflineCatalog catalog(store);
  catalog.load();
  auto guard = std::make_shared<FakeStorageGuard>(0, 0);
  DownloadQueue queue(store, catalog, guard);
  queue.setRetryDelaysMs({10, 10});
  QStringList completed;
  QObject::connect(&queue, &DownloadQueue::completed, [&](const QString& id) { completed.append(id); });
  queue.setAccess(ServerId, UserId, server.url(), "MediaBrowser Token=\"" + Token + "\"");

  const QJsonObject source = episodeSource(itemId, content.size());
  REQUIRE(queue.enqueueOriginal(episode(itemId, source), source) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !completed.isEmpty(); }, 10000));

  const auto m = catalog.find(ServerId, UserId, itemId);
  REQUIRE(m);
  CHECK(m->state == ItemState::Complete);
  CHECK(m->format == 2);
  const QString base = QStringLiteral("Shows/Alice-Bob Show/Season 01/Alice-Bob Show - S01E02 - The - Pilot_");
  CHECK(m->mediaFile == base + QStringLiteral(".mkv"));
  CHECK(readFile(store.mediaPath(*m)) == content);
  CHECK(m->audioFiles.value(1) == base + QStringLiteral(".ru.mka"));
  const QByteArray audio = readFile(store.filePath(*m, m->audioFiles.value(1)));
  CHECK(audio.contains("cccc"));
  CHECK_FALSE(audio.contains("WWWW"));
  // The tiny picture's request asks for the external track, copied.
  bool asked = false;
  for (const auto& request : server.requests())
    if (request.path.endsWith(QStringLiteral("/stream.mkv"))) asked = true;
  CHECK(asked);
  // Internal files are out of the library.
  CHECK(QFileInfo::exists(store.itemDir(*m) + QStringLiteral("/item.json")));
  CHECK(store.itemDir(*m).contains(QStringLiteral("/.jellyfin-desktop/")));

  // It plays from disk with the dub as an external track.
  OfflineLocalSource local(store, catalog);
  local.setUser(ServerId, UserId);
  local.setOnline(false);
  const auto copy = local.find(itemId);
  REQUIRE(copy);
  REQUIRE(copy->externalAudio.size() == 1);
  CHECK(copy->externalAudio.first().streamIndex == 1);

  // Deleting removes the library files and the folders left empty.
  REQUIRE(store.removeItem(*m));
  CHECK_FALSE(QFileInfo::exists(store.root() + QStringLiteral("/Shows")));
  CHECK_FALSE(QFileInfo::exists(store.itemDir(*m)));
}

TEST_CASE("Old downloads move to the library layout at start-up") {
  QTemporaryDir dir;
  DownloadStore store(dir.filePath(QStringLiteral("downloads")));
  const QString itemId = itemIdFor(42);
  const QJsonObject source = episodeSource(itemId, 4096);
  ItemManifest m;
  m.format = 1;
  m.serverId = ServerId;
  m.userId = UserId;
  m.itemId = itemId;
  m.state = ItemState::Complete;
  m.item = episode(itemId, source);
  m.mediaSource = source;
  m.mediaFile = QStringLiteral("media.mkv");
  m.expectedSize = 4096;
  m.subtitleFiles.insert(0, QStringLiteral("subs/0.srt"));
  REQUIRE(store.writeManifest(m));
  const QString old = store.legacyItemDir(ServerId, UserId, itemId);
  REQUIRE(writeFile(old + QStringLiteral("/media.mkv"), QByteArray(4096, 'm')));
  REQUIRE(writeFile(old + QStringLiteral("/subs/0.srt"), QByteArray("1\n")));
  REQUIRE(writeFile(old + QStringLiteral("/images/Primary"), QByteArray(10, 'i')));

  OfflineCatalog catalog(store);
  REQUIRE(catalog.load() == 1);
  CHECK(catalog.migrateLegacy() == 1);
  const auto moved = catalog.find(ServerId, UserId, itemId);
  REQUIRE(moved);
  CHECK(moved->format == 2);
  const QString base = QStringLiteral("Shows/Alice-Bob Show/Season 01/Alice-Bob Show - S01E02 - The - Pilot_");
  CHECK(moved->mediaFile == base + QStringLiteral(".mkv"));
  CHECK(moved->subtitleFiles.value(0) == base + QStringLiteral(".ru.forced.srt"));
  CHECK(QFileInfo(store.mediaPath(*moved)).size() == 4096);
  CHECK(QFileInfo::exists(store.filePath(*moved, moved->subtitleFiles.value(0))));
  CHECK(QFileInfo::exists(store.itemDir(*moved) + QStringLiteral("/images/Primary")));
  CHECK_FALSE(QFileInfo::exists(old));
  CHECK_FALSE(QFileInfo::exists(store.root() + QLatin1Char('/') + ServerId));

  // The next start finds it where it is now.
  OfflineCatalog again(store);
  REQUIRE(again.load() == 1);
  CHECK(again.migrateLegacy() == 0);
  CHECK(again.find(ServerId, UserId, itemId)->mediaFile == moved->mediaFile);
}
