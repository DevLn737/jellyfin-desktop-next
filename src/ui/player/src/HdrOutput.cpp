#include <jfd/ui/HdrOutput.h>

#include <QLoggingCategory>
#include <QQuickWindow>
#include <QSGRendererInterface>

#ifdef JFD_HDR_OUTPUT
#include <rhi/qrhi.h>
#endif

namespace jfd::ui {

  namespace {
    Q_LOGGING_CATEGORY(lcHdr, "jfd.player.hdr")
  } // namespace

  HdrOutput::HdrOutput(QObject* parent) : QObject(parent) {}

  bool HdrOutput::supported() {
#ifdef JFD_HDR_OUTPUT
    return true;
#else
    return false;
#endif
  }

  void HdrOutput::request(bool enabled) {
    if (enabled && supported() && qEnvironmentVariableIsEmpty("QSG_RHI_HDR")) qputenv("QSG_RHI_HDR", "hdr10");
  }

  QString HdrOutput::shader() {
#ifdef JFD_HDR_OUTPUT
    return QStringLiteral("qrc:/qt/qml/Jfd/Ui/Player/shaders/hdr10_ui.frag.qsb");
#else
    return QString();
#endif
  }

  void HdrOutput::setVideoLayer(QQuickItem* layer) {
    if (m_videoLayer == layer) return;
    if (m_videoLayer) disconnect(m_videoLayer, nullptr, this, nullptr);
    m_videoLayer = layer;
    if (layer) connect(layer, &QQuickItem::childrenChanged, this, &HdrOutput::videoLayerChanged);
    Q_EMIT videoLayerChanged();
  }

  bool HdrOutput::videoShown() const { return m_videoLayer && !m_videoLayer->childItems().isEmpty(); }

  void HdrOutput::watch(QQuickWindow* window) {
    if (!window || window == m_window) return;
    if (m_window) disconnect(m_window, nullptr, this, nullptr);
    m_window = window;
    if (!supported()) return;
    // The scene graph's sync: render thread, GUI thread blocked.
    connect(window, &QQuickWindow::beforeSynchronizing, this, [this, window] { readSwapChain(window); },
            Qt::DirectConnection);
    connect(window, &QWindow::screenChanged, this, [this] { m_dirty.storeRelease(1); });
  }

  void HdrOutput::readSwapChain([[maybe_unused]] QQuickWindow* window) {
#ifdef JFD_HDR_OUTPUT
    if (!m_dirty.testAndSetAcquire(1, 0)) return;
    auto* swapChain = static_cast<QRhiSwapChain*>(
        window->rendererInterface()->getResource(window, QSGRendererInterface::RhiSwapchainResource));
    if (!swapChain) {
      m_dirty.storeRelease(1);
      return;
    }
    const bool active = swapChain->format() == QRhiSwapChain::HDR10;
    const float level = active ? swapChain->hdrInfo().sdrWhiteLevel : 0.f;
    const double sdrWhite = level > 0 ? static_cast<double>(level) : 203.0;
    QMetaObject::invokeMethod(this, [this, active, sdrWhite] { update(active, sdrWhite); }, Qt::QueuedConnection);
#endif
  }

  void HdrOutput::update(bool active, double sdrWhiteNits) {
    if (active == m_active && sdrWhiteNits == m_sdrWhiteNits) return;
    m_active = active;
    m_sdrWhiteNits = sdrWhiteNits;
    if (active)
      qCInfo(lcHdr) << "HDR10 output, SDR white at" << sdrWhiteNits << "nits";
    else
      qCInfo(lcHdr) << "SDR output";
    Q_EMIT changed();
  }

} // namespace jfd::ui
