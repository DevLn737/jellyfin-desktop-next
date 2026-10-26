#include "MpvVideoItem.h"
#include "MpvPlayer.h"
#include "RepaintRequest.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>

#include <mpv/render_gl.h>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include <qguiapplication_platform.h>
#endif

namespace jfd::media {

namespace {

void* getProcAddress(void*, const char* name)
{
  auto* context = QOpenGLContext::currentContext();
  if (!context)
    return nullptr;
  void* result = reinterpret_cast<void*>(context->getProcAddress(QByteArray(name)));
#ifdef Q_OS_WIN
  // WGL does not return OpenGL 1.1 entry points; they live in opengl32.dll.
  if (!result)
    if (HMODULE module = GetModuleHandleW(L"opengl32.dll"))
      result = reinterpret_cast<void*>(GetProcAddress(module, name));
#endif
  return result;
}

class MpvRenderer : public QObject, public QQuickFramebufferObject::Renderer
{
public:
  MpvRenderer() = default;

  ~MpvRenderer() override
  {
    // Runs on the render thread with the scene graph context current.
    if (m_context)
    {
      mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
      mpv_render_context_free(m_context);
    }
    delete m_repaint;
  }

  QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override
  {
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    format.setInternalTextureFormat(GL_RGBA8);
    return new QOpenGLFramebufferObject(size, format);
  }

  void synchronize(QQuickFramebufferObject* item) override
  {
    auto* video = static_cast<MpvVideoItem*>(item);
    if (!m_window && video->window())
    {
      m_window = video->window();
      QObject::connect(m_window, &QQuickWindow::frameSwapped, this, [this] {
        if (m_context && m_rendered)
          mpv_render_context_report_swap(m_context);
        m_rendered = false;
      }, Qt::DirectConnection);
    }
    if (!m_handle)
      m_handle = video->handle();
    if (!m_context && m_handle && !m_failed)
      createContext(video);
  }

  void render() override
  {
    if (!m_context)
      return;
    auto* fbo = framebufferObject();
    mpv_opengl_fbo target {static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), GL_RGBA8};
    // No flip: Qt Quick displays this framebuffer's texture with its first
    // row at the top, which is how mpv writes an unflipped image. (Flipping
    // is for drawing straight to a window's default framebuffer.)
    int flip = 0;
    mpv_render_param params[] {
      {MPV_RENDER_PARAM_OPENGL_FBO, &target},
      {MPV_RENDER_PARAM_FLIP_Y, &flip},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
    // Qt's state must not leak into mpv's drawing: blending, scissor and
    // above all sampler objects, which resetOpenGLState() leaves bound. A Qt
    // sampler that asks for mipmaps makes mpv's single-level textures
    // incomplete and the video renders black (seen with llvmpipe).
    QQuickOpenGLUtils::resetOpenGLState();
    unbindSamplers();
    m_rendered = mpv_render_context_render(m_context, params) >= 0;
    // mpv leaves its own GL state (programs, textures, blending) bound.
    QQuickOpenGLUtils::resetOpenGLState();
  }

private:
  void unbindSamplers()
  {
    if (m_samplerUnits == 0)
      return;
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    for (GLuint unit = 0; unit < m_samplerUnits; ++unit)
      gl->glBindSampler(unit, 0);
  }

  void createContext(MpvVideoItem* item)
  {
    auto* context = QOpenGLContext::currentContext();
    const auto format = context->format();
    const bool samplerObjects = context->isOpenGLES()
      ? format.majorVersion() >= 3
      : format.majorVersion() > 3 || (format.majorVersion() == 3 && format.minorVersion() >= 3)
        || context->hasExtension(QByteArrayLiteral("GL_ARB_sampler_objects"));
    if (samplerObjects)
    {
      GLint units = 0;
      context->functions()->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &units);
      // mpv binds at most a handful of textures; Qt Quick fewer still.
      m_samplerUnits = static_cast<GLuint>(std::clamp(units, 0, 32));
    }

    mpv_opengl_init_params glParams {getProcAddress, nullptr};
    // mpv needs the window system's display for VA-API zero-copy (and VDPAU)
    // interop; without it hardware decoding falls back to copying frames.
    mpv_render_param display {MPV_RENDER_PARAM_INVALID, nullptr};
#if defined(Q_OS_LINUX) && QT_CONFIG(xcb)
    if (auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
      display = {MPV_RENDER_PARAM_X11_DISPLAY, x11->display()};
#endif
#if defined(Q_OS_LINUX) && QT_CONFIG(wayland)
    if (auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
      display = {MPV_RENDER_PARAM_WL_DISPLAY, wayland->display()};
#endif
    mpv_render_param params[] {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
      display,
      {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (const int error = mpv_render_context_create(&m_context, m_handle.get(), params); error < 0)
    {
      m_context = nullptr;
      // Moving the video to another window frees the old window's context on
      // its own render thread, possibly a moment after this one asks for a
      // new one (mpv allows one per core): ask again on the next frame.
      if (++m_attempts < MaxAttempts)
      {
        QMetaObject::invokeMethod(item, [item] { item->update(); }, Qt::QueuedConnection);
        return;
      }
      m_failed = true;
      item->reportRenderError(QString::fromUtf8(mpv_error_string(error)));
      return;
    }
    m_repaint = new RepaintRequest(item);
    mpv_render_context_set_update_callback(m_context, [](void* request) {
      static_cast<RepaintRequest*>(request)->request();
    }, m_repaint);
    QString glRenderer;
    if (const auto* name = context->functions()->glGetString(GL_RENDERER))
      glRenderer = QString::fromUtf8(reinterpret_cast<const char*>(name));
    item->reportRenderContextReady(glRenderer);
  }

  QQuickWindow* m_window = nullptr;
  MpvHandle m_handle;
  mpv_render_context* m_context = nullptr;
  RepaintRequest* m_repaint = nullptr;
  GLuint m_samplerUnits = 0;
  bool m_rendered = false;
  bool m_failed = false;
  int m_attempts = 0;
  static constexpr int MaxAttempts = 100;
};

}

MpvVideoItem::MpvVideoItem(QQuickItem* parent) : QQuickFramebufferObject(parent)
{
  setTextureFollowsItemSize(true);
}

void MpvVideoItem::setPlayer(MpvPlayer* player)
{
  if (m_player == player)
    return;
  // One core per item: the render context is bound to it for the item's life.
  if (m_handle && player && player->handle() != m_handle)
  {
    qWarning("MpvVideoItem: the player can be set only once");
    return;
  }
  m_player = player;
  if (player)
    m_handle = player->handle();
  emit playerChanged();
  update();
}

MpvHandle MpvVideoItem::handle() const
{
  return m_handle;
}

QQuickFramebufferObject::Renderer* MpvVideoItem::createRenderer() const
{
  return new MpvRenderer();
}

void MpvVideoItem::reportRenderContextReady(const QString& renderer)
{
  m_status.setReady(renderer);
  emit renderContextReady(renderer);
}

void MpvVideoItem::reportRenderError(const QString& message)
{
  m_status.setFailed(message);
  emit renderError(message);
}

}
