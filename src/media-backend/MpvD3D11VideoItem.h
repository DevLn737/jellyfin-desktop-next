#pragma once

#include "MpvHandle.h"

#include <QPointer>
#include <QQuickItem>

namespace jfd::media {

class MpvPlayer;

// The Direct3D 11 counterpart of MpvVideoItem, with the same QML interface.
// mpv draws into a texture on Qt Quick's own ID3D11Device (the D3D11 render
// API of libmpv 2.6), which the scene graph shows as an ordinary texture node,
// so QML can stack, clip and blur it like any item. d3d11va decodes on the
// same device without copying frames back to the CPU.
//
// All mpv render-context calls happen on the scene graph render thread,
// between Qt's beginExternalCommands() and endExternalCommands(), with the
// immediate context state cleared before and after mpv draws.
class MpvD3D11VideoItem : public QQuickItem
{
  Q_OBJECT
  // moc needs the complete type for the property; this header sorts before
  // MpvPlayer.h in the combined moc file.
  Q_MOC_INCLUDE("MpvPlayer.h")
  Q_PROPERTY(jfd::media::MpvPlayer* player READ player WRITE setPlayer NOTIFY playerChanged)

public:
  explicit MpvD3D11VideoItem(QQuickItem* parent = nullptr);

  MpvPlayer* player() const { return m_player; }
  void setPlayer(MpvPlayer* player);
  MpvHandle handle() const { return m_handle; }

signals:
  void playerChanged();
  // Emitted on the render thread after the first mpv render context is ready.
  void renderContextReady(QString renderer);
  void renderError(QString message);

protected:
  QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData* data) override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
  QPointer<MpvPlayer> m_player;
  MpvHandle m_handle;
  bool m_renderFailed = false;
};

}
