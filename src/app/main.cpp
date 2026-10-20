#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <cstdio>
#include <memory>

namespace {

struct MpvDeleter
{
  void operator()(mpv_handle* handle) const { mpv_terminate_destroy(handle); }
};
using MpvHandle = std::unique_ptr<mpv_handle, MpvDeleter>;

void* getProcAddress(void*, const char* name)
{
  auto* context = QOpenGLContext::currentContext();
  return context ? reinterpret_cast<void*>(context->getProcAddress(name)) : nullptr;
}

// Phase 0 check: libmpv links and loads, and its OpenGL render context can be
// created on the Qt Quick render thread. Prints what it found and exits.
int runSmoke(QQmlApplicationEngine& engine, mpv_handle* mpv)
{
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
  if (!window)
    return 2;
  int result = 3;
  int swaps = 0;
  mpv_render_context* render = nullptr;
  QObject::connect(window, &QQuickWindow::beforeRendering, window, [&] {
    if (render)
      return;
    window->beginExternalCommands();
    auto* context = QOpenGLContext::currentContext();
    std::printf("OpenGL: %s | %s\n",
                reinterpret_cast<const char*>(context->functions()->glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(context->functions()->glGetString(GL_VERSION)));
    mpv_opengl_init_params glParams {getProcAddress, nullptr};
    mpv_render_param params[] {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
    const int error = mpv_render_context_create(&render, mpv, params);
    std::printf("mpv render context: %s\n", mpv_error_string(error));
    result = error < 0 ? 4 : 0;
    window->endExternalCommands();
  }, Qt::DirectConnection);
  QObject::connect(window, &QQuickWindow::sceneGraphInvalidated, window, [&] {
    if (render)
      mpv_render_context_free(render);
    render = nullptr;
  }, Qt::DirectConnection);
  QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
    if (++swaps == 5)
      QMetaObject::invokeMethod(qApp, [window] { window->close(); }, Qt::QueuedConnection);
    else
      window->update();
  }, Qt::DirectConnection);
  QTimer::singleShot(30000, qApp, [] { QCoreApplication::exit(5); });
  const int exitCode = QGuiApplication::exec();
  std::printf("frames swapped: %d\n", swaps);
  return exitCode != 0 ? exitCode : result;
}

}

int main(int argc, char* argv[])
{
  // libmpv renders only through OpenGL, so the whole scene uses it (ADR 0001).
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("Jellyfin Desktop Next"));
  const bool smoke = QCoreApplication::arguments().contains(QStringLiteral("--smoke"));

  MpvHandle mpv(mpv_create());
  if (!mpv)
    return 1;
  mpv_set_option_string(mpv.get(), "config", "no");
  mpv_set_option_string(mpv.get(), "vo", "libmpv");
  mpv_set_option_string(mpv.get(), "terminal", "no");
  if (mpv_initialize(mpv.get()) < 0)
    return 1;
  const unsigned long api = mpv_client_api_version();
  char* version = mpv_get_property_string(mpv.get(), "mpv-version");
  std::printf("libmpv client API %lu.%lu, %s\n", api >> 16, api & 0xffff, version);
  mpv_free(version);

  QQmlApplicationEngine engine;
  engine.loadFromModule("JellyfinDesktop", "Main");
  if (engine.rootObjects().isEmpty())
    return 1;
  if (smoke)
    return runSmoke(engine, mpv.get());
  return QGuiApplication::exec();
}
