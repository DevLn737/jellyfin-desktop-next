#include "LibraryTestSupport.h"

#include <jfd/library/ImageCache.h>

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

using namespace jfd;
using jfd::library::ImageCache;
using namespace jfd::test;

namespace {
  QByteArray png(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
  }

  struct Images {
    int delayMs = 0;
    HttpResponse operator()(const HttpRequest& r) const {
      if (r.path.startsWith(QLatin1String("/missing"))) return {404, {}, {}, 0};
      return HttpResponse{200, png(800, 1200) + QByteArray(), "image/png", delayMs};
    }
  };

  QUrl at(LibraryFixture& f, const QString& path) {
    return f.client.url(path);
  }
} // namespace

TEST_CASE("Images are decoded at the card size, kept in memory and shared between waiters") {
  Images images;
  LibraryFixture f([&images](const HttpRequest& r) { return images(r); });
  ImageCache cache(f.client, {});
  QImage a;
  QImage b;
  cache.request(at(f, QStringLiteral("/Items/1/Images/Primary")), QSize(200, 300), nullptr,
                [&](const QImage& image) { a = image; });
  cache.request(at(f, QStringLiteral("/Items/1/Images/Primary")), QSize(100, 150), nullptr,
                [&](const QImage& image) { b = image; });
  REQUIRE(waitFor([&] { return !a.isNull() && !b.isNull(); }));
  // One download for both sizes.
  CHECK(f.server.requests().size() == 1);
  CHECK(a.size() == QSize(200, 300));
  CHECK(b.size() == QSize(100, 150));

  // From memory now, before request() returns.
  bool immediate = false;
  cache.request(at(f, QStringLiteral("/Items/1/Images/Primary")), QSize(200, 300), nullptr,
                [&](const QImage& image) { immediate = image.size() == QSize(200, 300); });
  CHECK(immediate);
  CHECK(f.server.requests().size() == 1);

  // Failures come back as null images.
  bool failed = false;
  cache.request(at(f, QStringLiteral("/missing")), QSize(10, 10), nullptr,
                [&](const QImage& image) { failed = image.isNull(); });
  REQUIRE(waitFor([&] { return failed; }));
}

TEST_CASE("Memory stays under its limit however many images are loaded") {
  Images images;
  LibraryFixture f([&images](const HttpRequest& r) { return images(r); });
  ImageCache::Limits limits;
  // Room for about five 200x300 RGB32 images (240 KB each).
  limits.memoryBytes = 5 * 240'000 + 1000;
  ImageCache cache(f.client, {}, limits);
  int done = 0;
  int last = -1;
  for (int i = 0; i < 30; ++i)
    cache.request(at(f, QStringLiteral("/Items/%1/Images/Primary").arg(i)), QSize(200, 300),
                  nullptr, [&done, &last, i](const QImage&) {
                    ++done;
                    last = i;
                  });
  REQUIRE(waitFor([&] { return done == 30; }, 10000));
  CHECK(cache.memoryBytes() <= limits.memoryBytes);
  CHECK(cache.memoryBytes() > 0);
  // The most recently loaded are kept.
  CHECK_FALSE(
      cache.cached(at(f, QStringLiteral("/Items/%1/Images/Primary").arg(last)), QSize(200, 300))
          .isNull());
}

TEST_CASE("Newest requests go first, and cancelled ones never reach the network") {
  Images images;
  images.delayMs = 100;
  LibraryFixture f([&images](const HttpRequest& r) { return images(r); });
  ImageCache::Limits limits;
  limits.parallelDownloads = 1;
  ImageCache cache(f.client, {}, limits);
  QList<int> order;
  QList<ImageCache::Ticket> tickets;
  for (int i = 0; i < 5; ++i)
    tickets.append(cache.request(at(f, QStringLiteral("/Items/%1/Images/Primary").arg(i)),
                                 QSize(20, 30), nullptr,
                                 [&order, i](const QImage&) { order.append(i); }));
  // The cards scrolled past.
  cache.cancel(tickets[1]);
  cache.cancel(tickets[2]);
  CHECK(cache.pendingDownloads() == 2);
  REQUIRE(waitFor([&] { return order.size() == 3; }, 5000));
  waitFor([] { return false; }, 300);
  CHECK(order == QList<int>{0, 4, 3});
  CHECK(f.server.requests().size() == 3);
  CHECK(cache.runningDownloads() == 0);
}

TEST_CASE("A waiter whose context is gone is not called") {
  Images images;
  images.delayMs = 100;
  LibraryFixture f([&images](const HttpRequest& r) { return images(r); });
  ImageCache cache(f.client, {});
  bool called = false;
  auto context = std::make_unique<QObject>();
  cache.request(at(f, QStringLiteral("/Items/1/Images/Primary")), QSize(20, 30), context.get(),
                [&](const QImage&) { called = true; });
  context.reset();
  REQUIRE(waitFor([&] { return cache.runningDownloads() == 0; }));
  waitFor([] { return false; }, 50);
  CHECK_FALSE(called);
}

TEST_CASE("The disk cache survives a restart and stays under its limit") {
  Images images;
  LibraryFixture f([&images](const HttpRequest& r) { return images(r); });
  QTemporaryDir dir;
  const int fileSize = static_cast<int>(png(800, 1200).size());
  ImageCache::Limits limits;
  limits.diskBytes = fileSize * 3 + 10;
  {
    ImageCache cache(f.client, dir.path(), limits);
    int done = 0;
    for (int i = 0; i < 6; ++i)
      cache.request(at(f, QStringLiteral("/Items/%1/Images/Primary").arg(i)), QSize(20, 30),
                    nullptr, [&](const QImage&) { ++done; });
    REQUIRE(waitFor([&] { return done == 6; }, 10000));
    CHECK(cache.diskBytes() <= limits.diskBytes);
  }
  const auto requests = f.server.requests().size();
  ImageCache again(f.client, dir.path(), limits);
  CHECK(again.diskBytes() > 0);
  QImage image;
  again.request(at(f, QStringLiteral("/Items/5/Images/Primary")), QSize(20, 30), nullptr,
                [&](const QImage& i) { image = i; });
  // Read from disk, synchronously.
  CHECK_FALSE(image.isNull());
  CHECK(f.server.requests().size() == requests);
}

TEST_CASE("The token goes only to the client's own server; keys leave credentials out") {
  Images images;
  LibraryFixture f([&images](const HttpRequest& r) { return images(r); });
  FakeServer other([](const HttpRequest&) { return HttpResponse{200, png(4, 4), "image/png", 0}; });
  ImageCache cache(f.client, {});
  int done = 0;
  cache.request(at(f, QStringLiteral("/Videos/e1/Trickplay/320/0.jpg")), {}, nullptr,
                [&](const QImage&) { ++done; });
  cache.request(other.url(QStringLiteral("/poster.png")), {}, nullptr,
                [&](const QImage&) { ++done; });
  REQUIRE(waitFor([&] { return done == 2; }));
  CHECK(f.server.requests().front().headers.value("authorization").contains("token1"));
  CHECK_FALSE(other.requests().front().headers.contains("authorization"));

  CHECK(ImageCache::cacheKey(QUrl(QStringLiteral("http://h/x.jpg?ApiKey=secret&tag=1"))) ==
        QStringLiteral("http://h/x.jpg?tag=1"));
}
