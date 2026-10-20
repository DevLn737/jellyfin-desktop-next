#include "MpvD3D11VideoItem.h"
#include "MpvPlayer.h"
#include "RepaintRequest.h"

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
    m_rhiTexture.reset();
    delete m_repaint;
  }

  // Scene graph sync (render thread, GUI thread blocked). Returns false
  // after a failure, which has been reported through renderError().
  bool sync(MpvD3D11VideoItem* item)
  {
    if (!m_handle)
      m_handle = item->handle();
    if (!m_context && !m_failed)
      createContext(item);
    if (!m_context)
      return false;
    const QSize size = (item->size() * m_window->effectiveDevicePixelRatio()).toSize().expandedTo(QSize(1, 1));
    if (!m_texture || size != m_size)
      if (!createTexture(item, size))
        return false;
    setRect(item->boundingRect());
    m_renderPending = true;
    return true;
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

  void createContext(MpvD3D11VideoItem* item)
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
      return fail(item, QString::fromUtf8(mpv_error_string(error)));
    }
    m_repaint = new RepaintRequest(item);
    mpv_render_context_set_update_callback(m_context, [](void* request) {
      static_cast<RepaintRequest*>(request)->request();
    }, m_repaint);
    const bool hdr = setUpHdr(item);
    item->reportRenderContextReady(QStringLiteral("Direct3D 11 (feature level %1.%2%3): %4")
                                    .arg(m_device->GetFeatureLevel() >> 12)
                                    .arg((m_device->GetFeatureLevel() >> 8) & 0xf)
                                    .arg(hdr ? QStringLiteral(", HDR10") : QString())
                                    .arg(adapterName(m_device.Get())));
  }

  // HDR experiment (--hdr hdr10): when Qt Quick created an HDR10 swap chain,
  // mpv renders PQ BT.2020 for the display's peak into a 10-bit texture. Qt
  // passes the texture's values through unchanged; the QML content is encoded
  // to PQ at the SDR white level by the window. Without an HDR swap chain mpv
  // keeps its SDR output and tone-maps HDR files itself.
  bool setUpHdr(MpvD3D11VideoItem* item)
  {
    auto* swapChain = static_cast<QRhiSwapChain*>(
      m_window->rendererInterface()->getResource(m_window, QSGRendererInterface::RhiSwapchainResource));
    if (!swapChain || swapChain->format() != QRhiSwapChain::HDR10)
      return false;
    const QRhiSwapChainHdrInfo info = swapChain->hdrInfo();
    const double peak = info.limitsType == QRhiSwapChainHdrInfo::LuminanceInNits
        && info.limits.luminanceInNits.maxLuminance > 0
      ? static_cast<double>(info.limits.luminanceInNits.maxLuminance)
      : 1000.0;
    const double sdrWhite = info.sdrWhiteLevel > 0 ? static_cast<double>(info.sdrWhiteLevel) : 203.0;
    mpv_set_property_string(m_handle.get(), "target-prim", "bt.2020");
    mpv_set_property_string(m_handle.get(), "target-trc", "pq");
    mpv_set_property_string(m_handle.get(), "target-peak", QByteArray::number(peak, 'f', 0).constData());
    m_format = DXGI_FORMAT_R10G10B10A2_UNORM;
    item->reportHdr(sdrWhite, peak);
    return true;
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
    // same format (fromNative() would assume RGBA8); all three are replaced
    // together when the item's size in pixels changes.
    auto* rhi = static_cast<QRhi*>(
      m_window->rendererInterface()->getResource(m_window, QSGRendererInterface::RhiResource));
    std::unique_ptr<QRhiTexture> rhiTexture(rhi ? rhi->newTexture(
      m_format == DXGI_FORMAT_R10G10B10A2_UNORM ? QRhiTexture::RGB10A2 : QRhiTexture::RGBA8, size) : nullptr);
    if (!rhiTexture || !rhiTexture->createFrom({quint64(reinterpret_cast<quintptr>(created.Get())), 0}))
    {
      fail(item, QStringLiteral("Qt Quick could not use the video texture"));
      return false;
    }
    QSGTexture* wrapped = m_window->createTextureFromRhiTexture(rhiTexture.get(), QQuickWindow::TextureIsOpaque);
    QSGTexture* previous = texture();
    setTexture(wrapped);
    delete previous;
    m_rhiTexture = std::move(rhiTexture);
    m_texture = created;
    m_size = size;
    return true;
  }

  void fail(MpvD3D11VideoItem* item, const QString& message)
  {
    m_failed = true;
    item->reportRenderError(message);
  }

  QQuickWindow* m_window;
  MpvHandle m_handle;
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_deviceContext;
  ComPtr<ID3D11Texture2D> m_texture;
  std::unique_ptr<QRhiTexture> m_rhiTexture;
  QSize m_size;
  DXGI_FORMAT m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
  if (!node->sync(this))
  {
    // Already reported through renderError(); stop trying.
    m_renderFailed = true;
    delete node;
    return nullptr;
  }
  node->markDirty(QSGNode::DirtyMaterial);
  return node;
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

void MpvD3D11VideoItem::reportHdr(double sdrWhiteNits, double displayPeakNits)
{
  QMetaObject::invokeMethod(this, [this, sdrWhiteNits, displayPeakNits] {
    m_hdrActive = true;
    m_sdrWhiteNits = sdrWhiteNits;
    m_displayPeakNits = displayPeakNits;
    emit hdrChanged();
  }, Qt::QueuedConnection);
}

}
