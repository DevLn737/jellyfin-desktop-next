#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QCoreApplication>

// Qt networking needs an application object and an event loop; tests spin
// the loop with jfd::test::waitFor.
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  doctest::Context context(argc, argv);
  return context.run();
}
