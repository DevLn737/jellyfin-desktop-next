#pragma once

#include <jfd/offline/StorageGuard.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <atomic>

// Server JSON for downloaded items, with the secrets a real PlaybackInfo
// answer can carry, and a storage guard whose free space the test sets.
namespace jfd::test {

  inline const QString ServerId = QStringLiteral("0123456789abcdef0123456789abcdef");
  inline const QString UserId = QStringLiteral("a11cea11cea11cea11cea11cea11ce01");
  inline const QByteArray Token = "tok3n5ecret0offline";

  inline QString itemIdFor(int n) {
    return QStringLiteral("feedc0de%1").arg(n, 24, 10, QLatin1Char('0'));
  }

  inline QJsonObject mediaSourceJson(const QString& id, qint64 size,
                                     const QString& container = QStringLiteral("mkv")) {
    const QString secret = QString::fromLatin1(Token);
    QJsonObject source{
        {QStringLiteral("Id"), id},
        {QStringLiteral("Protocol"), QStringLiteral("File")},
        {QStringLiteral("Path"),
         QStringLiteral("/media/movies/Alice in the Test (2020)/film.") + container},
        {QStringLiteral("Container"), container},
        {QStringLiteral("VideoType"), QStringLiteral("VideoFile")},
        {QStringLiteral("ETag"), QStringLiteral("c0ffee")},
        {QStringLiteral("Bitrate"), 8000000},
        {QStringLiteral("SupportsDirectPlay"), true},
        {QStringLiteral("TranscodingUrl"), QStringLiteral("/videos/") + id +
                                               QStringLiteral("/master.m3u8?MediaSourceId=") + id +
                                               QStringLiteral("&ApiKey=") + secret},
        {QStringLiteral("DirectStreamUrl"),
         QStringLiteral("/Videos/") + id + QStringLiteral("/stream?static=true&api_key=") + secret},
        {QStringLiteral("MediaStreams"),
         QJsonArray{
             QJsonObject{{QStringLiteral("Index"), 2},
                         {QStringLiteral("Type"), QStringLiteral("Subtitle")},
                         {QStringLiteral("DeliveryUrl"),
                          QStringLiteral("/Videos/") + id +
                              QStringLiteral("/Subtitles/2/0/Stream.srt?api_key=") + secret}}}},
    };
    if (size > 0) source.insert(QStringLiteral("Size"), static_cast<double>(size));
    return source;
  }

  inline QJsonObject itemJson(const QString& id, const QJsonObject& source,
                              const QString& type = QStringLiteral("Movie")) {
    return QJsonObject{
        {QStringLiteral("Id"), id},
        {QStringLiteral("Name"), QStringLiteral("Alice in the Test")},
        {QStringLiteral("ServerId"), ServerId},
        {QStringLiteral("Type"), type},
        {QStringLiteral("MediaType"),
         type == QLatin1String("Audio") ? QStringLiteral("Audio") : QStringLiteral("Video")},
        {QStringLiteral("VideoType"), QStringLiteral("VideoFile")},
        {QStringLiteral("RunTimeTicks"), 72000000000.0},
        {QStringLiteral("Overview"), QStringLiteral("Bob asks: is a=b? Nobody knows.")},
        {QStringLiteral("MediaSources"), QJsonArray{source}},
    };
  }

  // Free space as the test says.
  class FakeStorageGuard : public offline::StorageGuard {
  public:
    using StorageGuard::StorageGuard;
    std::atomic<std::int64_t> free{std::int64_t{1} << 40};
    mutable std::atomic<int> queries{0};
    std::optional<std::int64_t> freeBytes(const QString&) const override {
      ++queries;
      const std::int64_t value = free.load();
      if (value < 0) return std::nullopt;
      return value;
    }
  };

  // Every file under `root` whose bytes contain any of `needles`
  // (case-insensitive).
  inline QStringList filesContaining(const QString& root, const QList<QByteArray>& needles) {
    QStringList hits;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = it.next();
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) continue;
      const QByteArray data = file.readAll().toLower();
      for (const QByteArray& needle : needles)
        if (data.contains(needle.toLower())) hits.append(path);
    }
    return hits;
  }

} // namespace jfd::test
