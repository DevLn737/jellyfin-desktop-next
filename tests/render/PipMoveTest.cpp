#include "MpvPlayer.h"
#include "MpvVideoItem.h"
#ifdef JFD_HAVE_D3D11_VIDEO
#include "MpvD3D11VideoItem.h"
#endif

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

using jfd::media::MpvPlayer;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
  std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
  if (!condition)
    ++failures;
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs)
{
  QElapsedTimer timer;
  timer.start();
  while (!condition())
  {
    if (timer.elapsed() > timeoutMs)
      return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return true;
}

// The video moves between two windows the way picture in picture does it:
// the item in one window goes away and a new one is made in the other, on the
// same mpv core, while the file keeps playing. Item is MpvVideoItem (OpenGL)
// or MpvD3D11VideoItem; on Direct3D 11 the new context often has to be asked
// for again while the old window still holds the core's only one.
template <class Item>
struct Slot
{
  std::unique_ptr<QQuickWindow> window = std::make_unique<QQuickWindow>();
  QPointer<Item> video;
  bool ready = false;
  QString error;

  Slot(int x)
  {
    window->resize(320, 180);
    window->setPosition(x, 40);
    window->setColor(Qt::black);
    window->show();
  }

  void attach(MpvPlayer* player)
  {
    ready = false;
    error.clear();
    auto* item = new Item(window->contentItem());
    item->setSize(window->size());
    item->setPlayer(player);
    QObject::connect(item, &Item::renderContextReady, window.get(), [this] { ready = true; }, Qt::QueuedConnection);
    QObject::connect(item, &Item::renderError, window.get(), [this](const QString& e) { error = e; }, Qt::QueuedConnection);
    video = item;
  }

  void detach()
  {
    delete video.data();
    video = nullptr;
  }

  bool isBlue() const
  {
    const QImage image = window->grabWindow().convertToFormat(QImage::Format_RGB32);
    const QColor c = image.pixelColor(image.width() / 2, image.height() / 4);
    return c.blueF() > 0.7 && c.redF() < 0.3 && c.greenF() < 0.3;
  }
};

}

template <class Item>
int run(QGuiApplication& app)
{
  MpvPlayer::Options options;
  options.hwdec = QStringLiteral("no");
  options.audioOutput = QStringLiteral("null");
  options.loopFile = true;
  for (const char* scaler : {"scale", "dscale", "cscale"})
    options.extra.append({QByteArray(scaler), QByteArrayLiteral("bilinear")});
  auto player = std::make_unique<MpvPlayer>(options);
  check(player->isValid(), "mpv core starts");
  if (!player->isValid())
    return 1;

  Slot<Item> big(0), small(400);
  big.attach(player.get());
  check(waitFor([&] { return big.ready || !big.error.isEmpty(); }, 20000) && big.error.isEmpty(), "render context in the first window");

  bool firstFrame = false;
  QObject::connect(player.get(), &MpvPlayer::firstFrame, &app, [&] { firstFrame = true; });
  player->load(QStringLiteral("av://lavfi:color=c=0x0000ff:s=320x180:r=30"));
  check(waitFor([&] { return firstFrame; }, 20000), "first frame");
  waitFor([] { return false; }, 500);
  check(big.isBlue(), "video shows in the first window");

  QObject::connect(player.get(), &MpvPlayer::endOfFile, &app, [](const QString& r) { std::printf("     eof: %s\n", qPrintable(r)); });
  QObject::connect(player.get(), &MpvPlayer::error, &app, [](const QString& r) { std::printf("     error: %s\n", qPrintable(r)); });
  QObject::connect(player.get(), &MpvPlayer::firstFrame, &app, [](double) { std::printf("     first frame again\n"); });
  for (int round = 1; round <= 3; ++round)
  {
    Slot<Item>& from = round % 2 ? big : small;
    Slot<Item>& to = round % 2 ? small : big;
    const double before = player->position();
    player->setStringProperty("vid", "no");
    waitFor([] { return false; }, 100);
    from.detach();
    to.attach(player.get());
    const bool created = waitFor([&] { return to.ready || !to.error.isEmpty(); }, 10000);
    player->setStringProperty("vid", "auto");
    std::printf("     round %d: ready=%d error=%s\n", round, to.ready, qPrintable(to.error));
    check(created && to.error.isEmpty(), "render context in the other window after the move");
    waitFor([] { return false; }, 2000);
    {
      const QImage image = to.window->grabWindow().convertToFormat(QImage::Format_RGB32);
      const QColor c = image.pixelColor(image.width() / 2, image.height() / 4);
      std::printf("     pixel %.2f %.2f %.2f, position %.2f -> %.2f\n", c.redF(), c.greenF(), c.blueF(), before, player->position());
    }
    check(to.isBlue(), "video shows in the other window");
    check(player->position() != before, "playback goes on across the move");
  }
  return failures ? 1 : 0;
}

// "jfd_pip_move_test d3d11" moves the Direct3D 11 item; without an argument
// the OpenGL one.
int main(int argc, char* argv[])
{
  const bool d3d11 = argc > 1 && std::strcmp(argv[1], "d3d11") == 0;
#ifndef JFD_HAVE_D3D11_VIDEO
  if (d3d11)
  {
    std::printf("FAIL this build has no Direct3D 11 video item\n");
    return 1;
  }
#endif
  QQuickWindow::setGraphicsApi(d3d11 ? QSGRendererInterface::Direct3D11 : QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
#ifdef JFD_HAVE_D3D11_VIDEO
  if (d3d11)
    return run<jfd::media::MpvD3D11VideoItem>(app);
#endif
  return run<jfd::media::MpvVideoItem>(app);
}
