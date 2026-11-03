#include "MpvD3D11VideoItem.h"
#include "MpvPlayer.h"
#include "RepaintRequest.h"
#include "RenderSetup.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QtQuick/qsgtexture_platform.h>
#include <rhi/qrhi.h>

#include <mpv/render_d3d11.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <memory>

namespace jfd::media {

namespace {

using Microsoft::WRL::ComPtr;

QString adapterName(ID3D11Device* device)
{
  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC description {};
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&adapter))
      || FAILED(adapter->GetDesc(&description)))
    return QStringLiteral("unknown adapter");
  return QString::fromWCharArray(description.Description);
}

// Owns the mpv render context and the texture mpv draws into. Lives on the
// render thread: the scene graph creates it in updatePaintNode() and deletes
// it with the scene, before the D3D11 device goes away.
class D3D11VideoNode : public QObject, public QSGSimpleTextureNode
{
public:
  explicit D3D11VideoNode(QQuickWindow* window) : m_window(window)
  {
    setOwnsTexture(false);
    setFiltering(QSGTexture::Linear);
    // Outside the render pass: mpv draws into its own texture first, then
    // Qt records the frame that samples it.
    connect(window, &QQuickWindow::beforeRendering, this, &D3D11VideoNode::render, Qt::DirectConnection);
    connect(window, &QQuickWindow::frameSwapped, this, [this] {
      if (m_context && m_rendered)
        mpv_render_context_report_swap(m_context);
      m_rendered = false;
    }, Qt::DirectConnection);
  }

  ~D3D11VideoNode() override
  {
    if (m_context)
    {
      mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
      mpv_render_context_free(m_context);
    }
    delete texture();
    delete m_repaint;
  }

  // Scene graph sync (render thread, GUI thread blocked). Failed has been
  // reported through renderError(); Retry asks again on the next frame.
  RenderSetup sync(MpvD3D11VideoItem* item)
  {
    if (!m_handle)
      m_handle = item->handle();
    if (m_failed)
      return RenderSetup::Failed;
    if (!m_context)
    {
      const RenderSetup created = createContext(item);
      if (created != RenderSetup::Ready)
        return created;
    }
    // The window may have moved to another display, or its swap chain
    // changed format: mpv's output follows it.
    if (const HdrTarget target = hdrTarget(); target != m_hdr)
      applyHdr(item, target);
    const QSize size = (item->size() * m_window->effectiveDevicePixelRatio()).toSize().expandedTo(QSize(1, 1));
    if (!m_texture || size != m_size || m_textureFormat != m_format)
      if (!createTexture(item, size))
        return RenderSetup::Failed;
    setRect(item->boundingRect());
    m_renderPending = true;
    return RenderSetup::Ready;
  }

private:
  void render()
  {
    if (!m_renderPending || !m_context || !m_texture)
      return;
    m_renderPending = false;
    m_window->beginExternalCommands();
    // mpv expects the stages it does not use at their defaults and leaves its
    // own state bound; endExternalCommands() makes Qt set all of its state
    // again for the rest of the frame.
    m_deviceContext->ClearState();
    mpv_d3d11_fbo target {m_texture.Get()};
    mpv_render_param params[] {
      {MPV_RENDER_PARAM_D3D11_FBO, &target},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
    m_rendered = mpv_render_context_render(m_context, params) >= 0;
    m_deviceContext->ClearState();
    m_window->endExternalCommands();
  }

  RenderSetup createContext(MpvD3D11VideoItem* item)
  {
    auto* renderer = m_window->rendererInterface();
    if (renderer->graphicsApi() != QSGRendererInterface::Direct3D11)
      return fail(item, QStringLiteral("Qt Quick is not rendering with Direct3D 11"));
    m_device = static_cast<ID3D11Device*>(renderer->getResource(m_window, QSGRendererInterface::DeviceResource));
    m_deviceContext = static_cast<ID3D11DeviceContext*>(
      renderer->getResource(m_window, QSGRendererInterface::DeviceContextResource));
    if (!m_device || !m_deviceContext)
      return fail(item, QStringLiteral("Qt Quick did not provide its Direct3D 11 device"));

    mpv_d3d11_init_params init {m_device.Get()};
    mpv_render_param params[] {
      {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_D3D11)},
      {MPV_RENDER_PARAM_D3D11_INIT_PARAMS, &init},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (const int error = mpv_render_context_create(&m_context, m_handle.get(), params); error < 0)
    {
      m_context = nullptr;
      // See RenderSetup::Retry: the old window's context may not be freed
      // yet. The item counts the attempts, since it drops this node.
      if (item->contextAttempts().failed() == RenderSetup::Retry)
      {
        QMetaObject::invokeMethod(item, [item] { item->update(); }, Qt::QueuedConnection);
        return RenderSetup::Retry;
      }
      return fail(item, QString::fromUtf8(mpv_error_string(error)));
    }
    item->contextAttempts().succeeded();
    m_repaint = new RepaintRequest(item);
    mpv_render_context_set_update_callback(m_context, [](void* request) {
      static_cast<RepaintRequest*>(request)->request();
    }, m_repaint);
    m_hdr = hdrTarget();
    applyHdr(item, m_hdr);
    item->reportRenderContextReady(QStringLiteral("Direct3D 11 (feature level %1.%2%3): %4")
                                    .arg(m_device->GetFeatureLevel() >> 12)
                                    .arg((m_device->GetFeatureLevel() >> 8) & 0xf)
                                    .arg(m_hdr.active ? QStringLiteral(", HDR10") : QString())
                                    .arg(adapterName(m_device.Get())));
    return RenderSetup::Ready;
  }

  // HDR experiment (--hdr hdr10): when Qt Quick created an HDR10 swap chain,
  // mpv renders PQ BT.2020 for the display's peak into a 10-bit texture. Qt
  // passes the texture's values through unchanged; the QML content is encoded
  // to PQ at the SDR white level by the window. Without an HDR swap chain mpv
  // keeps its SDR output and tone-maps HDR files itself.
  struct HdrTarget
  {
    bool active = false;
    double peak = 0;
    double sdrWhite = 0;
    bool operator==(const HdrTarget&) const = default;
  };

  HdrTarget hdrTarget() const
  {
    auto* swapChain = static_cast<QRhiSwapChain*>(
      m_window->rendererInterface()->getResource(m_window, QSGRendererInterface::RhiSwapchainResource));
    if (!swapChain || swapChain->format() != QRhiSwapChain::HDR10)
      return {};
    const QRhiSwapChainHdrInfo info = swapChain->hdrInfo();
    HdrTarget target;
    target.active = true;
    target.peak = info.limitsType == QRhiSwapChainHdrInfo::LuminanceInNits
        && info.limits.luminanceInNits.maxLuminance > 0
      ? static_cast<double>(info.limits.luminanceInNits.maxLuminance)
      : 1000.0;
    target.sdrWhite = info.sdrWhiteLevel > 0 ? static_cast<double>(info.sdrWhiteLevel) : 203.0;
    return target;
  }

  // The target is a property of the shared core, so an SDR window resets
  // what an HDR window set before (PiP, another display). The writes are
  // asynchronous: a blocking client call on the render thread can deadlock
  // with a core that waits for this thread.
  void applyHdr(MpvD3D11VideoItem* item, const HdrTarget& target)
  {
    m_hdr = target;
    const QByteArray peak = target.active ? QByteArray::number(target.peak, 'f', 0) : QByteArrayLiteral("auto");
    setPropertyAsync("target-prim", target.active ? QByteArrayLiteral("bt.2020") : QByteArrayLiteral("auto"));
    setPropertyAsync("target-trc", target.active ? QByteArrayLiteral("pq") : QByteArrayLiteral("auto"));
    setPropertyAsync("target-peak", peak);
    m_format = target.active ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    item->reportHdr(target.active, target.sdrWhite, target.peak);
  }

  void setPropertyAsync(const char* name, const QByteArray& value)
  {
    // mpv copies the value before returning; replies with id 0 are ignored.
    const char* data = value.constData();
    mpv_set_property_async(m_handle.get(), 0, name, MPV_FORMAT_STRING, &data);
  }

  bool createTexture(MpvD3D11VideoItem* item, const QSize& size)
  {
    D3D11_TEXTURE2D_DESC description {};
    description.Width = static_cast<UINT>(size.width());
    description.Height = static_cast<UINT>(size.height());
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = m_format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> created;
    if (const HRESULT result = m_device->CreateTexture2D(&description, nullptr, &created); FAILED(result))
    {
      fail(item, QStringLiteral("could not create a %1x%2 video texture (0x%3)")
                   .arg(size.width()).arg(size.height())
                   .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0')));
      return false;
    }
    // Qt wraps the native texture without owning it, in a QRhiTexture of the
    // same format (fromNative() would assume RGBA8). The scene graph texture
    // owns that QRhiTexture; both are replaced, and the native texture with
    // them, when the item's size in pixels changes.
    auto* rhi = static_cast<QRhi*>(
      m_window->rendererInterface()->getResource(m_window, QSGRendererInterface::RhiResource));
    std::unique_ptr<QRhiTexture> rhiTexture(rhi ? rhi->newTexture(
      m_format == DXGI_FORMAT_R10G10B10A2_UNORM ? QRhiTexture::RGB10A2 : QRhiTexture::RGBA8, size) : nullptr);
    if (!rhiTexture || !rhiTexture->createFrom({quint64(reinterpret_cast<quintptr>(created.Get())), 0}))
    {
      fail(item, QStringLiteral("Qt Quick could not use the video texture"));
      return false;
    }
    QSGTexture* wrapped = m_window->createTextureFromRhiTexture(rhiTexture.release(), QQuickWindow::TextureIsOpaque);
    QSGTexture* previous = texture();
    setTexture(wrapped);
    delete previous;
    m_texture = created;
    m_size = size;
    m_textureFormat = m_format;
    return true;
  }

  RenderSetup fail(MpvD3D11VideoItem* item, const QString& message)
  {
    m_failed = true;
    item->reportRenderError(message);
    return RenderSetup::Failed;
  }

  QQuickWindow* m_window;
  MpvHandle m_handle;
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_deviceContext;
  ComPtr<ID3D11Texture2D> m_texture;
  QSize m_size;
  DXGI_FORMAT m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  DXGI_FORMAT m_textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  HdrTarget m_hdr;
  mpv_render_context* m_context = nullptr;
  RepaintRequest* m_repaint = nullptr;
  bool m_renderPending = false;
  bool m_rendered = false;
  bool m_failed = false;
};

}

MpvD3D11VideoItem::MpvD3D11VideoItem(QQuickItem* parent) : QQuickItem(parent)
{
  setFlag(ItemHasContents, true);
}

void MpvD3D11VideoItem::setPlayer(MpvPlayer* player)
{
  if (m_player == player)
    return;
  // One core per item: the render context is bound to it for the item's life.
  if (m_handle && player && player->handle() != m_handle)
  {
    qWarning("MpvD3D11VideoItem: the player can be set only once");
    return;
  }
  m_player = player;
  if (player)
    m_handle = player->handle();
  emit playerChanged();
  update();
}

QSGNode* MpvD3D11VideoItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
  auto* node = static_cast<D3D11VideoNode*>(oldNode);
  if (!node)
  {
    if (!m_handle || !window() || m_renderFailed)
      return nullptr;
    node = new D3D11VideoNode(window());
  }
  const RenderSetup setup = node->sync(this);
  if (setup == RenderSetup::Ready)
  {
    node->markDirty(QSGNode::DirtyMaterial);
    return node;
  }
  // Failed was reported through renderError(): stop trying. On Retry a
  // queued update() asks again with a new node, and the item counts the
  // attempts (contextAttempts()), so nothing stops it here.
  if (setup == RenderSetup::Failed)
    m_renderFailed = true;
  delete node;
  return nullptr;
}

void MpvD3D11VideoItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size())
    update();
}

void MpvD3D11VideoItem::reportRenderContextReady(const QString& renderer)
{
  m_status.setReady(renderer);
  emit renderContextReady(renderer);
}

void MpvD3D11VideoItem::reportRenderError(const QString& message)
{
  m_status.setFailed(message);
  emit renderError(message);
}

void MpvD3D11VideoItem::reportHdr(bool active, double sdrWhiteNits, double displayPeakNits)
{
  QMetaObject::invokeMethod(this, [this, active, sdrWhiteNits, displayPeakNits] {
    if (active == m_hdrActive && sdrWhiteNits == m_sdrWhiteNits && displayPeakNits == m_displayPeakNits)
      return;
    m_hdrActive = active;
    m_sdrWhiteNits = sdrWhiteNits;
    m_displayPeakNits = displayPeakNits;
    emit hdrChanged();
  }, Qt::QueuedConnection);
}

}
