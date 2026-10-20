#include "AppController.h"
#include "FrameRecorder.h"
#include "Measurement.h"
#include "MpvPlayer.h"
#include "MpvVideoItem.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <utility>
#include <memory>

using jfd::media::MpvPlayer;
using jfd::media::MpvVideoItem;

namespace {
QString toUrl(const QString& value)
{
  if (value.contains(QLatin1String("://")))
    return value; // http(s), av://lavfi:... and other mpv protocols.
  return QFileInfo(value).absoluteFilePath();
}
}

int main(int argc, char* argv[])
{
  // Unbuffered, so CI logs keep everything printed before a crash.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  // libmpv renders only through OpenGL, so the whole scene uses it (ADR 0001).
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("Jellyfin Desktop Next"));
  QGuiApplication::setApplicationVersion(QStringLiteral(JFD_VERSION));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Phase 1 prototype: video with a menu over it."));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files or URLs to play."));
  const QCommandLineOption fullscreen(QStringLiteral("fullscreen"), QStringLiteral("Start in fullscreen."));
  const QCommandLineOption animate(QStringLiteral("animate-menu"), QStringLiteral("Keep the menu animating (measurements)."));
  const QCommandLineOption stats(QStringLiteral("stats"), QStringLiteral("Show on-screen statistics."));
  const QCommandLineOption hwdec(QStringLiteral("hwdec"), QStringLiteral("mpv hwdec mode."), QStringLiteral("mode"), QStringLiteral("auto-safe"));
  const QCommandLineOption ao(QStringLiteral("ao"), QStringLiteral("mpv audio output."), QStringLiteral("driver"));
  const QCommandLineOption loop(QStringLiteral("loop"), QStringLiteral("Loop the file."));
  const QCommandLineOption scenario(QStringLiteral("scenario"), QStringLiteral("Measurement: frames, memory, fullscreen, seek, play."), QStringLiteral("name"));
  const QCommandLineOption report(QStringLiteral("report"), QStringLiteral("Measurement report (JSON)."), QStringLiteral("path"));
  const QCommandLineOption duration(QStringLiteral("duration"), QStringLiteral("Seconds to record."), QStringLiteral("seconds"), QStringLiteral("0"));
  const QCommandLineOption warmup(QStringLiteral("warmup"), QStringLiteral("Warm-up seconds."), QStringLiteral("seconds"), QStringLiteral("0"));
  const QCommandLineOption sample(QStringLiteral("sample-interval"), QStringLiteral("Memory sample seconds."), QStringLiteral("seconds"), QStringLiteral("5"));
  const QCommandLineOption replace(QStringLiteral("replace-interval"), QStringLiteral("Load the next file every N seconds."), QStringLiteral("seconds"), QStringLiteral("0"));
  const QCommandLineOption toggles(QStringLiteral("toggles"), QStringLiteral("Fullscreen round trips."), QStringLiteral("count"), QStringLiteral("50"));
  const QCommandLineOption seeks(QStringLiteral("seeks"), QStringLiteral("Seeks to time."), QStringLiteral("count"), QStringLiteral("20"));
  const QCommandLineOption smoke(QStringLiteral("smoke"), QStringLiteral("Start, render a few frames and exit (CI)."));
  parser.addOptions({fullscreen, animate, stats, hwdec, ao, loop, scenario, report, duration, warmup,
                     sample, replace, toggles, seeks, smoke});
  parser.process(app);

  QStringList files;
  for (const QString& file : parser.positionalArguments())
    files.append(toUrl(file));

  MpvPlayer::Options playerOptions;
  playerOptions.hwdec = parser.value(hwdec);
  playerOptions.audioOutput = parser.value(ao);
  // Measurements loop their file so a run never ends at the end of the file.
  playerOptions.loopFile = parser.isSet(loop) || parser.isSet(scenario);
  // Declared before the engine: QML is destroyed first, the render thread
  // frees the render context, then the mpv core is terminated.
  auto player = std::make_unique<MpvPlayer>(playerOptions);
  if (!player->isValid())
  {
    std::fprintf(stderr, "Could not start mpv\n");
    return 1;
  }
  AppController controller(player.get(), parser.isSet(animate));
  controller.setStatsVisible(parser.isSet(stats));

  qmlRegisterType<MpvVideoItem>("JellyfinDesktop.Media", 1, 0, "MpvVideo");
  qmlRegisterUncreatableType<MpvPlayer>("JellyfinDesktop.Media", 1, 0, "MpvPlayer", QStringLiteral("Provided by the application"));

  auto engine = std::make_unique<QQmlApplicationEngine>();
  engine->setInitialProperties({
    {QStringLiteral("player"), QVariant::fromValue(player.get())},
    {QStringLiteral("app"), QVariant::fromValue(&controller)},
  });
  engine->loadFromModule("JellyfinDesktop", "Main");
  auto* window = engine->rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine->rootObjects().first());
  if (!window)
    return 1;

  FrameRecorder frames(window);
  controller.attachWindow(window, &frames);
  auto* video = window->findChild<MpvVideoItem*>(QStringLiteral("video"));
  std::unique_ptr<Measurement> measurement;
  if (parser.isSet(scenario))
  {
    MeasurementOptions options;
    options.scenario = parser.value(scenario);
    options.files = files;
    options.durationSeconds = parser.value(duration).toInt();
    options.warmupSeconds = parser.value(warmup).toInt();
    options.sampleSeconds = parser.value(sample).toInt();
    options.replaceSeconds = parser.value(replace).toInt();
    options.toggles = parser.value(toggles).toInt();
    options.seeks = parser.value(seeks).toInt();
    options.reportPath = parser.value(report);
    measurement = std::make_unique<Measurement>(options, player.get(), window, &frames);
  }
  if (video)
  {
    QObject::connect(video, &MpvVideoItem::renderContextReady, &controller, [&](const QString& renderer) {
      controller.setGlRenderer(renderer);
      if (measurement)
        measurement->setGlRenderer(renderer);
    }, Qt::QueuedConnection);
    QObject::connect(video, &MpvVideoItem::renderError, &app, [](const QString& message) {
      std::fprintf(stderr, "Video renderer failed: %s\n", qPrintable(message));
      QCoreApplication::exit(3);
    }, Qt::QueuedConnection);
  }

  if (parser.isSet(fullscreen))
    window->showFullScreen();

  // mpv opens its video output only once the render context exists, so
  // playback starts after the first scene graph frame has created it.
  std::function<void()> startPlayback;
  int smokeResult = 0;
  if (parser.isSet(smoke))
  {
    // Plays a generated test video unless a file was given, waits for the
    // first frame and a few swaps, then closes like a user would.
    smokeResult = 4;
    startPlayback = [&] {
      player->load(files.value(0, QStringLiteral("av://lavfi:testsrc2=size=640x360:rate=30")));
    };
    QObject::connect(player.get(), &MpvPlayer::firstFrame, &app, [&](double ms) {
      std::printf("first frame after %.0f ms, hwdec: %s\n", ms, qPrintable(player->hwdecCurrent()));
      frames.restart();
      QTimer::singleShot(1000, window, [&] {
        const auto summary = frames.measurement().summary();
        std::printf("frame intervals in 1 s: %zu, mean %.1f ms, GL: %s\n", summary.frames,
                    summary.meanUs / 1000.0, qPrintable(controller.glRenderer()));
        smokeResult = summary.frames >= 3 && !controller.glRenderer().isEmpty() ? 0 : 5;
        window->close();
      });
    });
    QTimer::singleShot(30000, &app, [] { QCoreApplication::exit(6); });
  }
  else if (measurement)
    startPlayback = [&] { measurement->start(); };
  else if (!files.isEmpty())
    startPlayback = [&] { player->load(files.first()); };
  if (video)
    QObject::connect(video, &MpvVideoItem::renderContextReady, &app, [&] {
      if (startPlayback)
        std::exchange(startPlayback, nullptr)();
    }, Qt::QueuedConnection);

  QElapsedTimer shutdown;
  QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] { shutdown.start(); });
  const int code = QGuiApplication::exec();
  // Ordered shutdown: QML and the render context, then the mpv core.
  controller.detachWindow();
  measurement.reset();
  engine.reset(); // Destroys the window; the render thread frees the render context.
  player.reset();
  std::fprintf(stderr, "shutdown took %lld ms\n", static_cast<long long>(shutdown.isValid() ? shutdown.elapsed() : -1));
  return code != 0 ? code : smokeResult;
}
