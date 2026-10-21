#include "jfd/ui/GraphicsInfo.h"

#include <QMetaObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRegularExpression>
#include <rhi/qrhi.h>

#include <memory>

namespace jfd::ui {

  namespace {
    constexpr quint32 IntelVendor = 0x8086;

    GraphicsDevice describe(QQuickWindow& window) {
      GraphicsDevice device;
      QRhi* rhi = window.rhi();
      if (rhi == nullptr) {
        device.kind = GraphicsDevice::Kind::Software;
        device.api = QStringLiteral("software");
        return device;
      }
      device.api = QString::fromLatin1(rhi->backendName());
      const QRhiDriverInfo info = rhi->driverInfo();
      device.name = QString::fromUtf8(info.deviceName);
      device.vendorId = quint32(info.vendorId);
      switch (info.deviceType) {
      case QRhiDriverInfo::IntegratedDevice:
        device.kind = GraphicsDevice::Kind::Integrated;
        break;
      case QRhiDriverInfo::DiscreteDevice:
        device.kind = GraphicsDevice::Kind::Discrete;
        break;
      case QRhiDriverInfo::CpuDevice:
        device.kind = GraphicsDevice::Kind::Software;
        break;
      default:
        break;
      }
      return device;
    }
  } // namespace

  bool prefersReducedEffects(const GraphicsDevice& device) {
    switch (device.kind) {
    case GraphicsDevice::Kind::Integrated:
    case GraphicsDevice::Kind::Software:
      return true;
    case GraphicsDevice::Kind::Discrete:
      return false;
    case GraphicsDevice::Kind::Unknown:
      break;
    }
    const QString name = device.name.toLower();
    static const QRegularExpression software(
        QStringLiteral("llvmpipe|softpipe|swiftshader|basic render|\\bwarp\\b"));
    if (software.match(name).hasMatch()) {
      return true;
    }
    const bool intel = device.vendorId == IntelVendor || name.contains(QLatin1String("intel"));
    if (intel) {
      return !name.contains(QLatin1String("arc"));
    }
    // AMD APUs: "AMD Radeon(TM) Graphics", "Radeon 780M", "Radeon Vega 8
    // Graphics"; discrete cards carry "RX", "Pro" or "FirePro".
    static const QRegularExpression amdApu(
        QStringLiteral("radeon(\\(tm\\))? (graphics|\\d{3}m\\b|vega \\d+ graphics)|vega \\d+ graphics"));
    return amdApu.match(name).hasMatch() && !name.contains(QLatin1String(" rx "));
  }

  void probeGraphics(QQuickWindow& window, std::function<void(const GraphicsDevice&)> done) {
    auto callback = std::make_shared<std::function<void(const GraphicsDevice&)>>(std::move(done));
    QPointer<QQuickWindow> target(&window);
    const auto report = [target, callback] {
      if (target == nullptr || !*callback) {
        return;
      }
      auto run = std::move(*callback);
      *callback = nullptr;
      run(describe(*target));
    };
    if (window.rhi() != nullptr || window.isSceneGraphInitialized()) {
      report();
      return;
    }
    // Emitted on the render thread; the answer goes back to the window's.
    QObject::connect(&window, &QQuickWindow::sceneGraphInitialized, &window,
                     [target, report] {
                       if (target != nullptr) {
                         QMetaObject::invokeMethod(target.data(), report, Qt::QueuedConnection);
                       }
                     },
                     Qt::DirectConnection);
  }

} // namespace jfd::ui
