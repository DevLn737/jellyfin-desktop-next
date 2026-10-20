#include "AppController.h"
#include "FrameRecorder.h"
#include "Measurement.h"
#include "MpvPlayer.h"
#include "MpvVideoItem.h"
#ifdef JFD_HAVE_D3D11_VIDEO
#include "MpvD3D11VideoItem.h"
#endif

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <share.h>
#endif
#include <functional>
#include <utility>
#include <memory>
#include <mutex>

using jfd::media::MpvPlayer;
using jfd::media::MpvVideoItem;

namespace {
FILE* logOutput = nullptr;

// Timestamped lines in the format the baseline harness parses for both
// clients ("yyyy-MM-dd HH:mm:ss.zzz [category] message"), for example the
// qt.scenegraph.time.renderloop frame lines.
void installLogFile(const QString& path)
{
#ifdef Q_OS_WIN
  logOutput = _wfsopen(reinterpret_cast<const wchar_t*>(path.utf16()), L"a", _SH_DENYNO);
#else
  logOutput = std::fopen(QFile::encodeName(path).constData(), "a");
#endif
  if (!logOutput)
    return;
  static std::mutex mutex;
  qInstallMessageHandler([](QtMsgType, const QMessageLogContext& context, const QString& message) {
    const QByteArray line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toUtf8()
      + " [" + QByteArray(context.category ? context.category : "default") + "] " + message.toUtf8() + '\n';
    std::lock_guard lock(mutex);
    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), logOutput);
    std::fflush(logOutput);
  });
}

// The video renderer has to be chosen before QGuiApplication exists, so
// --renderer is read straight from argv: "gl", "d3d11" or "auto" (Direct3D 11
// where the build supports it, otherwise OpenGL).
bool useD3D11(int argc, char* argv[])
{
  const char* value = "auto";
  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc)
      value = argv[i + 1];
    else if (std::strncmp(argv[i], "--renderer=", 11) == 0)
      value = argv[i] + 11;
  }
#ifdef JFD_HAVE_D3D11_VIDEO
  return std::strcmp(value, "gl") != 0;
#else
  if (std::strcmp(value, "d3d11") == 0)
    std::fprintf(stderr, "This build has no Direct3D 11 video renderer; using OpenGL\n");
  return false;
#endif
}

// Both video items have the same signals; the app handles either.
template<typename VideoItem, typename Ready, typename Error>
void connectVideo(QQuickItem* item, QObject* context, Ready ready, Error error)
{
  if (auto* video = qobject_cast<VideoItem*>(item))
  {
    QObject::connect(video, &VideoItem::renderContextReady, context, ready, Qt::QueuedConnection);
    QObject::connect(video, &VideoItem::renderError, context, error, Qt::QueuedConnection);
  }
}

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
  // The whole scene uses the API mpv draws with (ADR 0001): Direct3D 11 on
  // Windows, OpenGL elsewhere and as the Windows fallback.
  const bool d3d11 = useD3D11(argc, argv);
  QQuickWindow::setGraphicsApi(d3d11 ? QSGRendererInterface::Direct3D11 : QSGRendererInterface::OpenGL);
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
  const QCommandLineOption maximized(QStringLiteral("maximized"), QStringLiteral("Start maximized."));
  const QCommandLineOption overlay(QStringLiteral("overlay"), QStringLiteral("menu, protocol or none."), QStringLiteral("name"), QStringLiteral("menu"));
  const QCommandLineOption mpvOption(QStringLiteral("mpv-option"), QStringLiteral("Extra mpv option, repeatable."), QStringLiteral("name=value"));
  const QCommandLineOption renderer(QStringLiteral("renderer"), QStringLiteral("Video renderer: auto, d3d11 or gl."), QStringLiteral("name"), QStringLiteral("auto"));
  const QCommandLineOption logFile(QStringLiteral("log-file"), QStringLiteral("Write Qt and mpv messages with timestamps."), QStringLiteral("path"));
  parser.addOptions({fullscreen, animate, stats, hwdec, ao, loop, scenario, report, duration, warmup,
                     sample, replace, toggles, seeks, smoke, maximized, overlay, mpvOption, renderer, logFile});
  parser.process(app);

  if (parser.isSet(logFile))
    installLogFile(parser.value(logFile));

  QStringList files;
  for (const QString& file : parser.positionalArguments())
    files.append(toUrl(file));

  MpvPlayer::Options playerOptions;
  playerOptions.hwdec = parser.value(hwdec);
  playerOptions.audioOutput = parser.value(ao);
  // Measurements loop their file so a run never ends at the end of the file.
  playerOptions.loopFile = parser.isSet(loop) || parser.isSet(scenario);
  for (const QString& option : parser.values(mpvOption))
  {
    const qsizetype equals = option.indexOf(QLatin1Char('='));
    playerOptions.extra.append({option.left(equals).toUtf8(), equals < 0 ? QByteArray("yes") : option.mid(equals + 1).toUtf8()});
  }
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
  controller.setOverlay(parser.value(overlay));

#ifdef JFD_HAVE_D3D11_VIDEO
  if (d3d11)
    qmlRegisterType<jfd::media::MpvD3D11VideoItem>("JellyfinDesktop.Media", 1, 0, "MpvVideo");
  else
#endif
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
  auto* video = window->findChild<QQuickItem*>(QStringLiteral("video"));
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
  // mpv opens its video output only once the render context exists, so
  // playback starts after the first scene graph frame has created it.
  std::function<void()> startPlayback;
  const auto contextReady = [&](const QString& name) {
    qInfo("video renderer: %s", qPrintable(name));
    controller.setGlRenderer(name);
    if (measurement)
      measurement->setGlRenderer(name);
    if (startPlayback)
      std::exchange(startPlayback, nullptr)();
  };
  const auto renderFailed = [](const QString& message) {
    std::fprintf(stderr, "Video renderer failed: %s\n", qPrintable(message));
    QCoreApplication::exit(3);
  };
  connectVideo<MpvVideoItem>(video, &controller, contextReady, renderFailed);
#ifdef JFD_HAVE_D3D11_VIDEO
  connectVideo<jfd::media::MpvD3D11VideoItem>(video, &controller, contextReady, renderFailed);
#endif

  if (parser.isSet(fullscreen))
    window->showFullScreen();
  else if (parser.isSet(maximized))
    window->showMaximized();

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
        std::printf("frame intervals in 1 s: %zu, mean %.1f ms, renderer: %s\n", summary.frames,
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
