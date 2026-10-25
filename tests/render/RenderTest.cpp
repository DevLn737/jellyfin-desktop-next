#include "MpvPlayer.h"
#include "MpvVideoItem.h"
#ifdef JFD_HAVE_D3D11_VIDEO
#include "MpvD3D11VideoItem.h"
#endif

#include <jfd/playback/PlayerOptions.h>

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <cstdio>
#include <cstring>
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

// Pixels of a colour inside a rectangle of the window.
int countPixels(const QImage& image, const QRect& rect, const std::function<bool(const QColor&)>& match)
{
  int n = 0;
  for (int y = rect.top(); y <= rect.bottom(); ++y)
    for (int x = rect.left(); x <= rect.right(); ++x)
      n += match(image.pixelColor(x, y)) ? 1 : 0;
  return n;
}

bool isYellow(const QColor& c) { return c.redF() > 0.75 && c.greenF() > 0.75 && c.blueF() < 0.45; }
bool isGreen(const QColor& c) { return c.greenF() > 0.6 && c.redF() < 0.35 && c.blueF() < 0.35; }

bool writeFile(const QString& path, const QByteArray& text)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
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

// Both video items have the same signals.
template<typename VideoItem>
void connectVideo(QQuickItem* item, QObject* context, bool& ready, QString& error)
{
  if (auto* video = qobject_cast<VideoItem*>(item))
  {
    QObject::connect(video, &VideoItem::renderContextReady, context, [&ready](const QString& name) {
      std::printf("     renderer: %s\n", qPrintable(name));
      ready = true;
    }, Qt::QueuedConnection);
    QObject::connect(video, &VideoItem::renderError, context, [&error](const QString& e) { error = e; }, Qt::QueuedConnection);
    // Already created before the connection (non-threaded render loop).
    if (!video->rendererName().isEmpty())
      ready = true;
    if (!video->renderFailure().isEmpty())
      error = video->renderFailure();
  }
}

}

// "jfd_render_test d3d11" tests the Direct3D 11 item; without an argument the
// OpenGL one.
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
    qmlRegisterType<jfd::media::MpvD3D11VideoItem>("JellyfinDesktop.Media", 1, 0, "MpvVideo");
  else
#endif
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
  auto* video = window->findChild<QQuickItem*>(QStringLiteral("video"));
  check(video != nullptr, "video item exists");
  if (!video)
    return 1;

  bool contextReady = false;
  QString renderError;
  connectVideo<MpvVideoItem>(video, &app, contextReady, renderError);
#ifdef JFD_HAVE_D3D11_VIDEO
  connectVideo<jfd::media::MpvD3D11VideoItem>(video, &app, contextReady, renderError);
#endif
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

  // Subtitle style (threshold "Субтитры" of stage 2b): the player's style
  // on plain subtitles, and on styled ones only when it overrides theirs.
  // The video is black; a long line of capitals gives enough pixels.
  QTemporaryDir subtitles;
  const QString srt = subtitles.filePath(QStringLiteral("plain.srt"));
  const QString ass = subtitles.filePath(QStringLiteral("styled.ass"));
  check(writeFile(srt, "1\n00:00:00,000 --> 10:00:00,000\nHHHHHHHHHH\n")
          && writeFile(ass,
               "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n\n"
               "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
               "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
               "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
               // Green (ASS colours are &HAABBGGRR), no outline, bottom centre.
               "Style: Default,Arial,24,&H0000FF00,&H0000FF00,&H00000000,&H00000000,1,0,0,0,100,100,0,0,1,0,0,2,10,10,"
               "10,1\n\n"
               "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
               "Dialogue: 0,0:00:00.00,9:00:00.00,Default,,0,0,0,,HHHHHHHHHH\n"),
        "subtitle files are written");
  const QRect videoTop(40, 60, 320, 90);
  const QRect videoBottom(40, 150, 320, 90);
  const auto grab = [&] { return window->grabWindow().convertToFormat(QImage::Format_RGB32); };
  // Enough coloured pixels in one half and none to speak of in the other.
  const auto shows = [&](const QRect& where, const QRect& elsewhere, bool (*colour)(const QColor&)) {
    int inside = 0, beside = 0;
    const bool ok = waitFor([&] {
      const QImage frame = grab();
      inside = countPixels(frame, where, colour);
      beside = countPixels(frame, elsewhere, colour);
      return inside > 80 && beside < 20;
    }, 10000);
    std::printf("     %d pixels where expected, %d elsewhere\n", inside, beside);
    return ok;
  };
  const auto loadWithSubtitle = [&](const QString& file) {
    firstFrame = false;
    player->setStringProperty("sub-files", file.toUtf8());
    player->load(QStringLiteral("av://lavfi:color=c=black:s=320x180:r=30"));
    return waitFor([&] { return firstFrame; }, 20000);
  };

  jfd::playback::SubtitleStyle style;
  style.color = QStringLiteral("#FBF93E");
  style.size = 60;
  style.borderSize = 0;
  player->setProperties(jfd::playback::subtitleProperties(style));
  check(loadWithSubtitle(srt), "video with a plain subtitle starts");
  check(shows(videoBottom, videoTop, isYellow), "plain subtitle takes the colour, at the bottom");
  style.placement = QStringLiteral("center,top");
  player->setProperties(jfd::playback::subtitleProperties(style));
  check(shows(videoTop, videoBottom, isYellow), "plain subtitle moves to the top");

  style.placement.clear();
  player->setProperties(jfd::playback::subtitleProperties(style));
  check(loadWithSubtitle(ass), "video with a styled subtitle starts");
  check(shows(videoBottom, videoTop, isGreen), "styled subtitle keeps its own colour");
  check(countPixels(grab(), videoBottom, isYellow) < 20, "player colour stays off the styled subtitle");
  style.assOverride = QStringLiteral("force");
  player->setProperties(jfd::playback::subtitleProperties(style));
  check(shows(videoBottom, videoTop, isYellow), "forced override gives the styled subtitle the player's colour");
  check(countPixels(grab(), videoBottom, isGreen) < 20, "its own colour is gone");

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
