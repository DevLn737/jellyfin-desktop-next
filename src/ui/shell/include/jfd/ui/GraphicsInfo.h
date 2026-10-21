#pragma once

#include <QString>
#include <QtGlobal>

#include <functional>

class QQuickWindow;

namespace jfd::ui {

  // The GPU the window renders on, as Qt reports it.
  struct GraphicsDevice {
    enum class Kind { Unknown, Integrated, Discrete, Software };
    Kind kind = Kind::Unknown;
    // PCI vendor id where the graphics API gives one (not OpenGL).
    quint32 vendorId = 0;
    // The adapter name, or GL_RENDERER with OpenGL.
    QString name;
    QString api;
  };

  // Whether "Simplified effects" on automatic should be on: integrated or
  // software graphics. Direct3D 11 and OpenGL do not say whether a GPU is
  // integrated, so the vendor and name decide: Intel other than Arc, and
  // AMD's APU names ("Radeon(TM) Graphics", "Radeon 780M", "Vega 8
  // Graphics"). Provisional until measured on the laptop.
  bool prefersReducedEffects(const GraphicsDevice& device);

  // Calls done on the window's thread once the scene graph is up, with the
  // GPU it renders on. Software rendering (no QRhi) reports Kind::Software.
  void probeGraphics(QQuickWindow& window, std::function<void(const GraphicsDevice&)> done);

} // namespace jfd::ui
