#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QGuiApplication>

// For tests that open windows (Qt Quick screens). Run them on a display or
// with QT_QPA_PLATFORM=offscreen.
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  doctest::Context context(argc, argv);
  return context.run();
}
