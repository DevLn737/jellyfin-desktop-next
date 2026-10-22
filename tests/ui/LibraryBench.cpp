// Timings of the library screens for the stage 2a thresholds
// (docs: /architecture thresholds "Запуск", "Главная и переходы",
// "Прокрутка библиотеки"). One measurement per run, printed as one JSON
// line; scripts/library-bench.sh runs it several times and checks the
// limits.
//
//   jfd_library_bench sign-in <profile>   signs in (JFD_TEST_* env) and keeps
//                                          the token in the profile folder
//   jfd_library_bench startup <profile>    from main() to the home page on
//                                          screen with the saved token, then
//                                          series, season and home again
//   jfd_library_bench scroll               a 1000-item library grid from an
//                                          in-process server, scrolled for a
//                                          few seconds; frame intervals
//
// Startup does not include loading the executable itself (tens of
// milliseconds); it starts at main().

#include "../support/FakeServer.h"

#include <jfd/platform/CredentialStore.h>
#include <jfd/ui/LibraryScreens.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <cstdio>
#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

using namespace jfd;

namespace {

  QElapsedTimer g_sinceMain;
  constexpr int Timeout = 20000;

  // Tokens in a file in the profile, so a later run starts signed in as the
  // application does with the system store.
  class FileCredentialStore final : public platform::CredentialStore {
  public:
    explicit FileCredentialStore(const QString& path) : m_file(path, QSettings::IniFormat) {}
    bool isAvailable() const override { return true; }
    ReadResult read(const QString& key) const override {
      const QString secret = m_file.value(key).toString();
      return {secret.isEmpty() ? Status::NotFound : Status::Ok, secret};
    }
    Status write(const QString& key, const QString& secret) override {
      m_file.setValue(key, secret);
      m_file.sync();
      return Status::Ok;
    }
    Status remove(const QString& key) override {
      m_file.remove(key);
      m_file.sync();
      return Status::Ok;
    }

  private:
    mutable QSettings m_file;
  };

  template <class Predicate> bool waitFor(Predicate done, int timeoutMs = Timeout) {
    return test::waitFor(done, timeoutMs);
  }

  struct Screens {
    explicit Screens(const QString& profile, std::unique_ptr<platform::CredentialStore> tokens) {
      ui::LibraryScreens::Config config;
      config.identity.version = QStringLiteral("0.0.1");
      config.identity.device = QStringLiteral("bench");
      config.identity.deviceId = QStringLiteral("jfd-library-bench");
      config.serversFile = profile + QStringLiteral("/servers.ini");
      config.imageDirectory = profile + QStringLiteral("/images");
      config.tokens = std::move(tokens);
      screens = std::make_unique<ui::LibraryScreens>(std::move(config));
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      navigator->setPlaceholderPage(
          QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      screens->install(engine, *navigator);
    }

    void load() {
      engine.loadData(R"(
        import QtQuick
        import Jfd.Ui.Shell
        Window {
            width: 1280
            height: 800
            visible: true
            AppShell { objectName: "shell"; anchors.fill: parent }
        })");
      window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
      QObject::connect(window, &QQuickWindow::frameSwapped, window, [this] { ++frames; });
    }

    QQuickItem* page() const {
      auto* stack = window->findChild<QQuickItem*>(QStringLiteral("pageStack"));
      return stack == nullptr ? nullptr : stack->property("currentItem").value<QQuickItem*>();
    }
    QObject* find(const char* name) const {
      QQuickItem* current = page();
      return current == nullptr ? nullptr : current->findChild<QObject*>(QString::fromLatin1(name));
    }
    int count(const char* name) const {
      QObject* object = find(name);
      return object == nullptr ? -1 : object->property("count").toInt();
    }
    bool showing(const QString& route) const {
      QQuickItem* current = page();
      return current != nullptr && current->property("route").toString() == route;
    }
    // Waits for the next frame on screen after `ready` holds.
    bool waitDrawn(const std::function<bool()>& ready) {
      if (!waitFor(ready)) return false;
      const int seen = frames;
      window->requestUpdate();
      return waitFor([&] { return frames > seen; });
    }

    QQmlApplicationEngine engine;
    std::unique_ptr<ui::LibraryScreens> screens;
    ui::Navigator* navigator = nullptr;
    QQuickWindow* window = nullptr;
    int frames = 0;
  };

  void print(const QJsonObject& result) {
    std::printf("%s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
  }

  int fail(const QString& what) {
    print({{QStringLiteral("error"), what}});
    return 1;
  }

  int signIn(const QString& profile) {
    QDir().mkpath(profile);
    library::ServerStore servers(profile + QStringLiteral("/servers.ini"));
    FileCredentialStore tokens(profile + QStringLiteral("/tokens.ini"));
    api::ClientIdentity identity;
    identity.version = QStringLiteral("0.0.1");
    identity.device = QStringLiteral("bench");
    identity.deviceId = QStringLiteral("jfd-library-bench");
    api::JellyfinClient client(identity);
    library::SignIn flow(client, servers, tokens);
    flow.connectToServer(qEnvironmentVariable("JFD_TEST_SERVER_URL"));
    if (!waitFor([&] { return flow.step() == library::SignIn::Step::Credentials; }))
      return fail(QStringLiteral("server not reached: ") + flow.errorText());
    flow.signIn(qEnvironmentVariable("JFD_TEST_USER"), qEnvironmentVariable("JFD_TEST_PASSWORD"));
    if (!waitFor([&] { return flow.step() == library::SignIn::Step::SignedIn; }))
      return fail(QStringLiteral("sign-in failed: ") + flow.errorText());
    print({{QStringLiteral("signedIn"), true}});
    return 0;
  }

  int startup(const QString& profile) {
    Screens screens(profile,
                    std::make_unique<FileCredentialStore>(profile + QStringLiteral("/tokens.ini")));
    if (!screens.screens->signIn().restore())
      return fail(QStringLiteral("no saved session; run sign-in first"));
    screens.load();
    if (!screens.waitDrawn([&] {
          return screens.showing(QStringLiteral("home")) && screens.count("homeRows") > 0 &&
                 screens.count("librariesRow") > 0;
        }))
      return fail(QStringLiteral("home did not show"));
    const qint64 startupMs = g_sinceMain.elapsed();

    // The first series on the home page, its season, then home again. The
    // rows fill in one by one, so the series may come after the first frame.
    const library::HomeModel& home = screens.screens->home();
    QString seriesId;
    const auto findSeries = [&] {
      for (int row = 0; row < home.count(); ++row) {
        const library::ItemListModel* items = home.rowItems(row);
        for (int i = 0; i < items->count(); ++i)
          if (items->entry(i)->item.type == QLatin1String("Series")) {
            seriesId = items->entry(i)->item.id;
            return true;
          }
      }
      return false;
    };
    if (!waitFor(findSeries)) return fail(QStringLiteral("no series on the home page"));

    QElapsedTimer step;
    step.start();
    screens.navigator->navigate(QStringLiteral("item/") + seriesId);
    if (!screens.waitDrawn([&] {
          QObject* name = screens.find("itemName");
          return screens.showing(QStringLiteral("item/") + seriesId) && name != nullptr &&
                 !name->property("text").toString().isEmpty() && screens.count("episodes") > 0;
        }))
      return fail(QStringLiteral("series page did not show"));
    const qint64 seriesMs = step.elapsed();

    const QString seasonId =
        screens.page()->property("details").value<QObject*>()->property("seasonId").toString();
    step.restart();
    screens.navigator->navigate(QStringLiteral("item/") + seasonId);
    if (!screens.waitDrawn([&] {
          return screens.showing(QStringLiteral("item/") + seasonId) &&
                 screens.count("episodes") > 0;
        }))
      return fail(QStringLiteral("season page did not show"));
    const qint64 seasonMs = step.elapsed();

    step.restart();
    screens.navigator->navigate(QStringLiteral("home"));
    if (!screens.waitDrawn([&] {
          return screens.showing(QStringLiteral("home")) && screens.count("homeRows") > 0;
        }))
      return fail(QStringLiteral("home did not show again"));
    const qint64 homeMs = step.elapsed();

    print({{QStringLiteral("startupMs"), startupMs},
           {QStringLiteral("seriesMs"), seriesMs},
           {QStringLiteral("seasonMs"), seasonMs},
           {QStringLiteral("homeMs"), homeMs}});
    return 0;
  }

  // --- Scrolling a large library ---------------------------------------------

  constexpr int LibrarySize = 1000;

  QByteArray poster(int index) {
    QImage image(300, 450, QImage::Format_RGB32);
    image.fill(QColor::fromHsv((index * 37) % 360, 160, 200));
    QPainter painter(&image);
    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Sans"), 48));
    painter.drawText(image.rect(), Qt::AlignCenter, QString::number(index));
    painter.end();
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 80);
    return jpeg;
  }

  test::HttpResponse json(const QJsonValue& value) {
    const QByteArray body = value.isArray()
                                ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    return test::HttpResponse{200, body, "application/json", 0};
  }

  QJsonObject movie(int index) {
    return {
        {QStringLiteral("Id"), QStringLiteral("m%1").arg(index)},
        {QStringLiteral("Name"), QStringLiteral("Movie %1").arg(index, 4, 10, QLatin1Char('0'))},
        {QStringLiteral("Type"), QStringLiteral("Movie")},
        {QStringLiteral("ProductionYear"), 1980 + index % 45},
        {QStringLiteral("ImageTags"),
         QJsonObject{{QStringLiteral("Primary"), QStringLiteral("t")}}},
        {QStringLiteral("UserData"), QJsonObject{{QStringLiteral("Played"), index % 7 == 0}}}};
  }

  test::HttpResponse fakeLibrary(const test::HttpRequest& request) {
    static QList<QByteArray> posters;
    if (posters.isEmpty())
      for (int i = 0; i < 24; ++i)
        posters.append(poster(i));
    const QString& path = request.path;
    if (path.contains(QLatin1String("/Images/"))) {
      const int index = path.section(QLatin1Char('/'), 2, 2).mid(1).toInt();
      return test::HttpResponse{200, posters[index % posters.size()], "image/jpeg", 0};
    }
    if (path == QLatin1String("/UserViews")) {
      QJsonObject view{{QStringLiteral("Id"), QStringLiteral("lib")},
                       {QStringLiteral("Name"), QStringLiteral("Movies")},
                       {QStringLiteral("Type"), QStringLiteral("CollectionFolder")},
                       {QStringLiteral("CollectionType"), QStringLiteral("movies")}};
      return json(QJsonObject{{QStringLiteral("Items"), QJsonArray{view}},
                              {QStringLiteral("TotalRecordCount"), 1}});
    }
    if (path == QLatin1String("/Items/Latest")) return json(QJsonArray{});
    if (path == QLatin1String("/Items/Filters"))
      return json(QJsonObject{{QStringLiteral("Genres"), QJsonArray{}},
                              {QStringLiteral("Years"), QJsonArray{}}});
    if (path == QLatin1String("/Items")) {
      const int start = request.query.queryItemValue(QStringLiteral("StartIndex")).toInt();
      const int limit = request.query.queryItemValue(QStringLiteral("Limit")).toInt();
      QJsonArray items;
      for (int i = start; i < std::min(LibrarySize, start + (limit > 0 ? limit : 100)); ++i)
        items.append(movie(i));
      return json(QJsonObject{{QStringLiteral("Items"), items},
                              {QStringLiteral("TotalRecordCount"), LibrarySize},
                              {QStringLiteral("StartIndex"), start}});
    }
    // Resume, Next up and anything else: nothing.
    return json(QJsonObject{{QStringLiteral("Items"), QJsonArray{}},
                            {QStringLiteral("TotalRecordCount"), 0}});
  }

  double percentile(QList<double> values, double p) {
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<qsizetype>(p * double(values.size() - 1) + 0.5);
    return values[std::min(index, values.size() - 1)];
  }

  int scroll() {
    test::FakeServer server(fakeLibrary);
    QTemporaryDir profile;
    Screens screens(profile.path(), std::make_unique<platform::MemoryCredentialStore>());
    api::JellyfinClient& client = screens.screens->client();
    client.setServerUrl(server.url());
    client.setSession(QStringLiteral("token"), QStringLiteral("user"));
    screens.screens->home().reload();
    if (!waitFor([&] { return screens.screens->home().libraries()->count() > 0; }))
      return fail(QStringLiteral("no library"));
    screens.navigator->reset(QStringLiteral("library/lib"));
    screens.load();
    if (!waitFor([&] { return screens.count("libraryGrid") == LibrarySize; }))
      return fail(QStringLiteral("grid did not fill"));
    // Let the first screen of cards and their images settle.
    test::waitFor([] { return false; }, 1500);

    auto* view = screens.find("libraryGrid")->property("view").value<QQuickItem*>();
    const double end = view->property("contentHeight").toDouble() - view->height();
    const double refreshRate =
        screens.window->screen() ? screens.window->screen()->refreshRate() : 60.0;
    const double period = 1000.0 / (refreshRate > 0 ? refreshRate : 60.0);
    // About a screen and a half per second, like a steady wheel scroll.
    const double speed = 1.5 * view->height() / 1000.0;

    QList<double> intervals;
    QElapsedTimer clock;
    clock.start();
    qint64 last = -1;
    bool done = false;
    const auto connection =
        QObject::connect(screens.window, &QQuickWindow::frameSwapped, view, [&] {
          const qint64 now = clock.nsecsElapsed();
          if (last >= 0) intervals.append(double(now - last) / 1e6);
          last = now;
          const double y = std::min(end, speed * double(now) / 1e6);
          view->setProperty("contentY", y);
          if (y >= end || now > 8'000'000'000LL) done = true;
          screens.window->requestUpdate();
        });
    screens.window->requestUpdate();
    waitFor([&] { return done; }, 15000);
    QObject::disconnect(connection);

    int slow = 0;
    for (double interval : intervals)
      if (interval > 1.25 * period) ++slow;
    print({{QStringLiteral("frames"), intervals.size()},
           {QStringLiteral("periodMs"), period},
           {QStringLiteral("p50Ms"), percentile(intervals, 0.5)},
           {QStringLiteral("p95Ms"), percentile(intervals, 0.95)},
           {QStringLiteral("maxMs"), percentile(intervals, 1.0)},
           {QStringLiteral("slowFrames"), slow},
           {QStringLiteral("scrolledPx"), view->property("contentY").toDouble()},
           {QStringLiteral("textureBytes"), screens.screens->textures().used()}});
    return 0;
  }

} // namespace

int main(int argc, char* argv[]) {
  g_sinceMain.start();
  QGuiApplication app(argc, argv);
  const QStringList args = app.arguments();
  const QString mode = args.value(1);
  if (mode == QLatin1String("sign-in")) return signIn(args.value(2));
  if (mode == QLatin1String("startup")) return startup(args.value(2));
  if (mode == QLatin1String("scroll")) return scroll();
  std::fprintf(stderr, "usage: %s sign-in <profile> | startup <profile> | scroll\n", argv[0]);
  return 2;
}
