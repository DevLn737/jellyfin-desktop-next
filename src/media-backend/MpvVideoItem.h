#pragma once

#include "MpvHandle.h"
#include "RenderStatus.h"

#include <QPointer>
#include <QQuickFramebufferObject>

namespace jfd::media {

class MpvPlayer;

// Draws mpv's video into an OpenGL framebuffer texture that is an ordinary
// Qt Quick texture node, so QML can stack, clip and blur it like any item.
// All mpv render-context calls happen on the scene graph render thread.
class MpvVideoItem : public QQuickFramebufferObject
{
  Q_OBJECT
  Q_PROPERTY(jfd::media::MpvPlayer* player READ player WRITE setPlayer NOTIFY playerChanged)

public:
  explicit MpvVideoItem(QQuickItem* parent = nullptr);

  MpvPlayer* player() const { return m_player; }
  void setPlayer(MpvPlayer* player);
  // The render context's description once it exists, or why it failed;
  // empty until then. Safe from any thread.
  QString rendererName() const { return m_status.renderer(); }
  QString renderFailure() const { return m_status.failure(); }
  // Render thread: records the outcome, then emits the matching signal.
  void reportRenderContextReady(const QString& renderer);
  void reportRenderError(const QString& message);

  MpvHandle handle() const;

  Renderer* createRenderer() const override;

signals:
  void playerChanged();
  // Emitted on the render thread after the first mpv render context is ready.
  void renderContextReady(QString glRenderer);
  void renderError(QString message);

private:
  QPointer<MpvPlayer> m_player;
  MpvHandle m_handle;
  RenderStatus m_status;
};

}
