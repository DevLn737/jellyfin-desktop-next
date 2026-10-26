#include "../support/DoctestQt.h"

#include <jfd/ui/CommandRegistry.h>
#include <jfd/ui/Navigator.h>

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

// Ctrl+K: the command palette over the shell.

using namespace jfd;

namespace {

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  void pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  void key(QQuickWindow& window, int code, Qt::KeyboardModifiers modifiers, const QString& text = {},
           quint32 scanCode = 0) {
    QKeyEvent press(QEvent::KeyPress, code, modifiers, scanCode, 0, 0, text);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers, scanCode, 0, 0, text);
    QCoreApplication::sendEvent(&window, &release);
  }

  struct Scene {
    Scene() {
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      navigator->reset(QStringLiteral("home"));
      commands = engine.singletonInstance<ui::CommandRegistry*>("Jfd.Ui.Shell", "CommandRegistry");
      window.resize(1000, 700);
      QQmlComponent component(&engine);
      component.setData(R"(
import QtQuick
import Jfd.Ui.Shell
import Jfd.Ui.Library
Item {
  width: 1000; height: 700
  AppShell { id: shell; anchors.fill: parent; onPaletteRequested: palette.toggle() }
  CommandPalette { id: palette; objectName: "palette"; anchors.fill: parent }
}
)", QUrl(QStringLiteral("qrc:/palette.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      root = qobject_cast<QQuickItem*>(object.get());
      root->setParentItem(window.contentItem());
      window.show();
      pump(50);
      palette = find(root, "palette");
      REQUIRE(palette != nullptr);
    }

    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> object;
    QQuickItem* root = nullptr;
    QQuickItem* palette = nullptr;
    ui::Navigator* navigator = nullptr;
    ui::CommandRegistry* commands = nullptr;
  };

} // namespace

TEST_CASE("Ctrl+K opens the palette with the field focused, quickly, and Esc closes it") {
  Scene scene;
  CHECK_FALSE(scene.palette->property("opened").toBool());
  QElapsedTimer timer;
  timer.start();
  key(scene.window, Qt::Key_K, Qt::ControlModifier);
  scene.window.grabWindow();
  const qint64 elapsed = timer.elapsed();
  CHECK(scene.palette->property("opened").toBool());
  MESSAGE("opened and drawn in ", elapsed, " ms");
  CHECK(elapsed < 100);
  QQuickItem* input = find(scene.palette, "paletteInput");
  REQUIRE(input != nullptr);
  CHECK(input->property("activeFocus").toBool());

  key(scene.window, Qt::Key_Escape, Qt::NoModifier);
  CHECK_FALSE(scene.palette->property("opened").toBool());
  // Ctrl+/ is the second way in.
  key(scene.window, Qt::Key_Slash, Qt::ControlModifier);
  CHECK(scene.palette->property("opened").toBool());
  key(scene.window, Qt::Key_Escape, Qt::NoModifier);
}

TEST_CASE("The K key opens it on another layout too") {
#ifdef Q_OS_LINUX
  Scene scene;
  // A Russian layout: the K key gives "л", but its scan code (37 + 8) is K's.
  key(scene.window, 0x06cc /* the keysym of Cyrillic el */, Qt::ControlModifier, QStringLiteral("л"), 45);
  CHECK(scene.palette->property("opened").toBool());
#endif
}

TEST_CASE("Commands: filter, arrows, Enter; the last one used comes first") {
  Scene scene;
  int ran = 0;
  QString last;
  for (const char* name : {"Open Music", "Open Requests", "Reset theme"}) {
    ui::CommandRegistry::Command command;
    command.id = QStringLiteral("test.") + QString::fromLatin1(name).toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
    command.title = QString::fromLatin1(name);
    command.run = [&ran, &last, title = command.title] {
      ++ran;
      last = title;
    };
    scene.commands->add(std::move(command));
  }
  ui::CommandRegistry::Command unavailable;
  unavailable.id = QStringLiteral("test.hidden");
  unavailable.title = QStringLiteral("Open Hidden");
  unavailable.available = [] { return false; };
  unavailable.run = [] {};
  scene.commands->add(std::move(unavailable));

  key(scene.window, Qt::Key_K, Qt::ControlModifier);
  const auto titles = [&] {
    QStringList list;
    for (const QVariant& entry : scene.palette->property("commandList").toList())
      list.append(entry.toMap().value(QStringLiteral("title")).toString());
    return list;
  };
  CHECK(titles().contains(QStringLiteral("Open Music")));
  CHECK_FALSE(titles().contains(QStringLiteral("Open Hidden")));

  // Typing filters.
  for (const QChar c : QStringLiteral("requ")) key(scene.window, c.toUpper().unicode(), Qt::NoModifier, QString(c));
  CHECK(titles() == QStringList{QStringLiteral("Open Requests")});
  key(scene.window, Qt::Key_Return, Qt::NoModifier);
  CHECK(ran == 1);
  CHECK(last == QStringLiteral("Open Requests"));
  CHECK_FALSE(scene.palette->property("opened").toBool());

  // Next time, with nothing typed, that command is first.
  key(scene.window, Qt::Key_K, Qt::ControlModifier);
  CHECK(titles().first() == QStringLiteral("Open Requests"));
  // Down and Enter choose the second one.
  key(scene.window, Qt::Key_Down, Qt::NoModifier);
  CHECK(scene.palette->property("current").toInt() == 1);
  key(scene.window, Qt::Key_Return, Qt::NoModifier);
  CHECK(ran == 2);
  CHECK(last != QStringLiteral("Open Requests"));
}

#include <QDir>

TEST_CASE("Palette screenshot") {
  const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
  if (dir.isEmpty()) return;
  QDir().mkpath(dir);
  Scene scene;
  for (const char* name : {"Open Home", "Open Search", "Open Settings", "Reset theme"}) {
    ui::CommandRegistry::Command command;
    command.id = QStringLiteral("shot.") + QString::fromLatin1(name);
    command.title = QString::fromLatin1(name);
    command.run = [] {};
    scene.commands->add(std::move(command));
  }
  key(scene.window, Qt::Key_K, Qt::ControlModifier);
  pump(300);
  CHECK(scene.window.grabWindow().save(dir + QStringLiteral("/palette-1000x700.png")));
}
