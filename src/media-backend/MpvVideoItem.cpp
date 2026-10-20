#include "MpvVideoItem.h"
#include "MpvPlayer.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>

#include <mpv/render_gl.h>

#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
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

// mpv's update callback may come from any thread; it only queues a coalesced
// repaint request for the item on the GUI thread.
class RepaintRequest
{
public:
  explicit RepaintRequest(QQuickItem* item) : m_item(item) {}
  void request()
  {
    if (m_pending.exchange(true))
      return;
    QMetaObject::invokeMethod(m_item, [this, item = m_item] {
      m_pending.store(false);
      if (item)
        item->update();
    }, Qt::QueuedConnection);
  }

private:
  QPointer<QQuickItem> m_item;
  std::atomic<bool> m_pending {false};
};

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
    m_rendered = mpv_render_context_render(m_context, params) >= 0;
    // mpv leaves its own GL state (programs, textures, blending) bound.
    QQuickOpenGLUtils::resetOpenGLState();
  }

private:
  void createContext(MpvVideoItem* item)
  {
    mpv_opengl_init_params glParams {getProcAddress, nullptr};
    mpv_render_param params[] {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (const int error = mpv_render_context_create(&m_context, m_handle.get(), params); error < 0)
    {
      m_failed = true;
      m_context = nullptr;
      emit item->renderError(QString::fromUtf8(mpv_error_string(error)));
      return;
    }
    m_repaint = new RepaintRequest(item);
    mpv_render_context_set_update_callback(m_context, [](void* request) {
      static_cast<RepaintRequest*>(request)->request();
    }, m_repaint);
    QString glRenderer;
    if (auto* context = QOpenGLContext::currentContext())
      if (const auto* name = context->functions()->glGetString(GL_RENDERER))
        glRenderer = QString::fromUtf8(reinterpret_cast<const char*>(name));
    emit item->renderContextReady(glRenderer);
  }

  QQuickWindow* m_window = nullptr;
  MpvHandle m_handle;
  mpv_render_context* m_context = nullptr;
  RepaintRequest* m_repaint = nullptr;
  bool m_rendered = false;
  bool m_failed = false;
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

}
