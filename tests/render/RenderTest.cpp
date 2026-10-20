#include "MpvPlayer.h"
#include "MpvVideoItem.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <cstdio>
#include <memory>

using jfd::media::MpvPlayer;
using jfd::media::MpvVideoItem;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
  std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
  if (!condition)
    ++failures;
}

// The average colour of a small block, to tolerate scaling and filtering.
QColor average(const QImage& image, int x, int y, int size = 8)
{
  double r = 0, g = 0, b = 0;
  int n = 0;
  for (int j = y; j < y + size; ++j)
    for (int i = x; i < x + size; ++i)
    {
      const QColor c = image.pixelColor(i, j);
      r += c.redF(); g += c.greenF(); b += c.blueF();
      ++n;
    }
  return QColor::fromRgbF(static_cast<float>(r / n), static_cast<float>(g / n), static_cast<float>(b / n));
}

bool near(const QColor& color, double r, double g, double b, double tolerance = 0.12)
{
  return std::abs(color.redF() - r) < tolerance && std::abs(color.greenF() - g) < tolerance
    && std::abs(color.blueF() - b) < tolerance;
}

void describe(const char* name, const QColor& c)
{
  std::printf("     %s: %.2f %.2f %.2f\n", name, c.redF(), c.greenF(), c.blueF());
}

// Waits on the event loop for a condition, up to a timeout.
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

}

int main(int argc, char* argv[])
{
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  qmlRegisterType<MpvVideoItem>("JellyfinDesktop.Media", 1, 0, "MpvVideo");
  qmlRegisterUncreatableType<MpvPlayer>("JellyfinDesktop.Media", 1, 0, "MpvPlayer", QStringLiteral("test"));

  MpvPlayer::Options options;
  options.hwdec = QStringLiteral("no");
  options.audioOutput = QStringLiteral("null");
  options.loopFile = true;
  // CI renders with Mesa llvmpipe, where mpv's LUT-based scalers (the lanczos
  // default) sometimes output black. This test checks orientation, blending
  // and containment, not scaling quality, so it uses the plain scalers.
  for (const char* scaler : {"scale", "dscale", "cscale"})
    options.extra.append({QByteArray(scaler), QByteArrayLiteral("bilinear")});
  auto player = std::make_unique<MpvPlayer>(options);
  check(player->isValid(), "mpv core starts");
  if (!player->isValid())
    return 1;

  auto engine = std::make_unique<QQmlApplicationEngine>();
  engine->setInitialProperties({{QStringLiteral("player"), QVariant::fromValue(player.get())}});
  engine->loadFromModule("JellyfinDesktop.RenderTest", "RenderScene");
  auto* window = qobject_cast<QQuickWindow*>(engine->rootObjects().value(0));
  check(window != nullptr, "scene loads");
  if (!window)
    return 1;
  auto* video = window->findChild<MpvVideoItem*>(QStringLiteral("video"));

  bool contextReady = false;
  QString renderError;
  QObject::connect(video, &MpvVideoItem::renderContextReady, &app, [&] { contextReady = true; }, Qt::QueuedConnection);
  QObject::connect(video, &MpvVideoItem::renderError, &app, [&](const QString& e) { renderError = e; }, Qt::QueuedConnection);
  check(waitFor([&] { return contextReady || !renderError.isEmpty(); }, 20000), "render context is created");
  check(renderError.isEmpty(), "no render error");

  // Blue top half, green bottom half: shows whether the image is upside down.
  bool firstFrame = false;
  QObject::connect(player.get(), &MpvPlayer::firstFrame, &app, [&] { firstFrame = true; });
  player->load(QStringLiteral(
    "av://lavfi:color=c=0x0000ff:s=320x90:r=30[top];color=c=0x00ff00:s=320x90:r=30[bottom];[top][bottom]vstack"));
  check(waitFor([&] { return firstFrame; }, 20000), "first frame");
  const double startPosition = player->position();
  waitFor([] { return false; }, 1500);
  check(player->position() > startPosition, "playback advances");

  const QImage image = window->grabWindow().convertToFormat(QImage::Format_RGB32)
    .scaled(window->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const QColor top = average(image, 200, 90);
  const QColor bottom = average(image, 200, 200);
  const QColor overlay = average(image, 60, 70);
  const QColor outside = average(image, 10, 10);
  const QColor white = average(image, 490, 50);
  describe("video top", top);
  describe("video bottom", bottom);
  describe("overlay over video", overlay);
  describe("outside video", outside);
  describe("white square", white);
  check(near(top, 0, 0, 1), "top of the video is blue (not flipped)");
  check(near(bottom, 0, 1, 0), "bottom of the video is green");
  check(near(overlay, 0.5, 0, 0.5, 0.15), "QML overlay blends over the video");
  check(near(outside, 0, 0, 0), "video stays inside its item");
  check(near(white, 1, 1, 1), "Qt items after mpv render normally");

  // Ordered teardown while playing: window and render context first, then mpv.
  QElapsedTimer teardown;
  teardown.start();
  engine.reset();
  player.reset();
  std::printf("     teardown: %lld ms\n", static_cast<long long>(teardown.elapsed()));
  check(teardown.elapsed() < 1000, "teardown under 1 s");

  std::printf("%s\n", failures == 0 ? "PASSED" : "FAILED");
  return failures == 0 ? 0 : 1;
}
