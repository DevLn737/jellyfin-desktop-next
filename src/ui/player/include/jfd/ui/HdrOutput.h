#pragma once

#include <QAtomicInteger>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <qqmlintegration.h>

namespace jfd::ui {

  // HDR10 output (the "playback/hdr10" setting, Windows with Direct3D 11).
  // With QSG_RHI_HDR=hdr10 Qt Quick asks for an HDR10 swap chain, and gets
  // one when the display is in HDR mode. Then mpv renders the video in PQ
  // BT.2020 for the display's peak, and everything else in the window has to
  // be encoded to PQ at the SDR white level: HdrScene (Main.qml) draws the
  // interface into a layer with the hdr10_ui shader and keeps the video
  // underneath, outside it. Without an HDR display Qt falls back to SDR,
  // `active` stays false and mpv tone-maps HDR files to SDR as usual.
  class HdrOutput : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(double sdrWhiteNits READ sdrWhiteNits NOTIFY changed)
    // The shader for the interface layer; empty without one.
    Q_PROPERTY(QString shader READ shader CONSTANT)
    // Where the player puts the video while `active` (HdrScene sets it).
    Q_PROPERTY(QQuickItem* videoLayer READ videoLayer WRITE setVideoLayer NOTIFY videoLayerChanged)
    // The video layer holds a video: the interface leaves its background out.
    Q_PROPERTY(bool videoShown READ videoShown NOTIFY videoLayerChanged)

  public:
    explicit HdrOutput(QObject* parent = nullptr);

    // This build can drive an HDR10 swap chain: Windows, the Direct3D 11
    // video item and the shader.
    static bool supported();
    // Before QGuiApplication: asks Qt Quick for an HDR10 swap chain when
    // `enabled` and supported, unless QSG_RHI_HDR is already set.
    static void request(bool enabled);

    bool active() const { return m_active; }
    double sdrWhiteNits() const { return m_sdrWhiteNits; }
    static QString shader();
    QQuickItem* videoLayer() const { return m_videoLayer; }
    void setVideoLayer(QQuickItem* layer);
    bool videoShown() const;

    // Follows the swap chain of `window`.
    Q_INVOKABLE void watch(QQuickWindow* window);

  Q_SIGNALS:
    void changed();
    void videoLayerChanged();

  private:
    void readSwapChain(QQuickWindow* window);
    void update(bool active, double sdrWhiteNits);

    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_videoLayer;
    bool m_active = false;
    double m_sdrWhiteNits = 203;
    // Set on the GUI thread when the window may have moved to a display
    // with other HDR info; read on the render thread.
    QAtomicInteger<int> m_dirty = 1;
  };

} // namespace jfd::ui
