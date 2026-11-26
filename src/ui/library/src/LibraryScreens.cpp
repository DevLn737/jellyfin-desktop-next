#include "jfd/ui/LibraryScreens.h"

#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QCoreApplication>
#include <QQmlEngine>

namespace jfd::ui {

  namespace {
    library::HomeModel::Options homeOptions(qreal ratio) {
      library::HomeModel::Options options;
      options.devicePixelRatio = ratio;
      return options;
    }

    library::SearchModel::Options searchOptions(qreal ratio) {
      library::SearchModel::Options options;
      options.devicePixelRatio = ratio;
      return options;
    }

    library::LiveTvChannels::Options liveTvOptions(qreal ratio) {
      library::LiveTvChannels::Options options;
      options.devicePixelRatio = ratio;
      return options;
    }

    library::LiveTvOnNow::Options onNowOptions(qreal ratio) {
      library::LiveTvOnNow::Options options;
      options.devicePixelRatio = ratio;
      return options;
    }

    const QString OnNowId = QStringLiteral("livetv.onNow");

    QUrl page(const char* name) {
      return QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Library/%1.qml").arg(QLatin1String(name)));
    }
  } // namespace

  LibraryScreens::LibraryScreens(account::Account& account, Config config)
      : m_account(account), m_devicePixelRatio(config.devicePixelRatio),
        m_images(account.client(), config.imageDirectory),
        m_home(account.client(), homeOptions(config.devicePixelRatio)),
        m_search(account.client(), searchOptions(config.devicePixelRatio)),
        m_liveTv(account.client(), liveTvOptions(config.devicePixelRatio)),
        m_onNow(account.client(), onNowOptions(config.devicePixelRatio)), m_recorder(account.client()),
        m_collections(account.client()),
        m_readFilters(std::move(config.readFilters)),
        m_writeFilters(std::move(config.writeFilters)),
        m_textures(std::make_shared<TextureBudget>(config.textureBytes)) {
    m_updates.attach(account.socket());
    m_home.follow(m_updates);
    m_search.follow(m_updates);
    m_collections.follow(m_updates);
  }

  LibraryScreens::~LibraryScreens() {
    // When the engine outlives this (it should not; see the header), QML
    // must not reach the services through the singleton, nor the loader
    // thread the image cache through the provider.
    if (m_ui) m_ui->attach({});
    if (m_engine) m_engine->removeImageProvider(QLatin1String(LibraryImageProvider::Id));
  }

  void LibraryScreens::syncOnNow() {
    library::HomeSections* sections = m_home.sections();
    if (!m_liveTv.available()) {
      sections->remove(OnNowId);
      m_onNow.clear();
      return;
    }
    if (sections->contains(OnNowId)) return;
    library::HomeSection row;
    row.id = OnNowId;
    row.title = QCoreApplication::translate("LiveTvSection", "On now");
    row.cardShape = QStringLiteral("thumb");
    // After Continue watching and Next up.
    row.defaultOrder = 30;
    row.model = &m_onNow;
    row.reload = [this] { m_onNow.reload(); };
    sections->add(std::move(row));
    m_onNow.reload();
  }

  bool LibraryScreens::install(QQmlEngine& engine, Navigator& navigator) {
    m_ui = engine.singletonInstance<LibraryUi*>("Jfd.Ui.Library", "Library");
    if (m_ui == nullptr) return false;
    m_engine = &engine;

    engine.addImageProvider(QLatin1String(LibraryImageProvider::Id),
                            new LibraryImageProvider(m_images, m_textures));

    LibraryUi::Services services;
    services.client = &m_account.client();
    services.signIn = &m_account.signIn();
    services.home = &m_home;
    services.search = &m_search;
    services.liveTv = &m_liveTv;
    services.recorder = &m_recorder;
    services.updates = &m_updates;
    services.collections = &m_collections;
    services.readFilters = m_readFilters;
    services.writeFilters = m_writeFilters;
    services.devicePixelRatio = m_devicePixelRatio;
    m_ui->attach(services);

    // Lost while the socket tries again; back once it is up again. The
    // screens reload on the way back.
    QObject::connect(&m_account.socket(), &api::SessionSocket::stateChanged, m_ui,
                     [ui = m_ui](api::SessionSocket::State state) {
                       using State = api::SessionSocket::State;
                       if (state == State::Reconnecting) ui->setOnline(false);
                       else if (state == State::Ready) ui->setOnline(true);
                     });

    navigator.registerPage(QStringLiteral("login"), page("LoginPage"));
    navigator.registerPage(QStringLiteral("home"), page("HomePage"));
    navigator.registerPage(QStringLiteral("library"), page("LibraryPage"));
    navigator.registerPage(QStringLiteral("collection"), page("CollectionPage"));
    navigator.registerPage(QStringLiteral("person"), page("PersonPage"));
    navigator.registerPage(QStringLiteral("item"), page("ItemPage"));
    navigator.registerPage(QStringLiteral("search"), page("SearchPage"));
    navigator.registerPage(QStringLiteral("customizeHome"), page("CustomizeHomePage"));
    // "What to show in Discord", from Settings → Integrations.
    RouteSpec discordPrivacy;
    discordPrivacy.name = QStringLiteral("discordPrivacy");
    discordPrivacy.page = page("DiscordPrivacyPage");
    navigator.registerRoute(discordPrivacy);

    // Live TV: listed only when the server has it for this user.
    RouteSpec liveTv;
    liveTv.name = QStringLiteral("livetv");
    liveTv.page = page("LiveTvPage");
    liveTv.hasSidebarEntry = true;
    liveTv.sidebar.icon = QStringLiteral("qrc:/qt/qml/Jfd/Ui/Library/icons/livetv.svg");
    liveTv.sidebar.text = QString::fromUtf8(QT_TRANSLATE_NOOP("Sidebar", "Live TV"));
    liveTv.sidebar.translationContext = "Sidebar";
    liveTv.sidebar.order = 160;
    liveTv.sidebar.placement = RouteSpec::Placement::LibraryTab;
    navigator.registerRoute(liveTv);
    navigator.setRouteAvailable(liveTv.name, m_liveTv.available());
    QObject::connect(&m_liveTv, &library::LiveTvChannels::availableChanged, &navigator,
                     [this, &navigator] {
                       navigator.setRouteAvailable(QStringLiteral("livetv"), m_liveTv.available());
                       syncOnNow();
                     });
    // The programme guide, from the Live TV screen; and one channel's page.
    RouteSpec guide;
    guide.name = QStringLiteral("guide");
    guide.page = page("GuidePage");
    navigator.registerRoute(guide);
    RouteSpec channel;
    channel.name = QStringLiteral("channel");
    channel.takesArgument = true;
    channel.page = page("ChannelPage");
    navigator.registerRoute(channel);
    // Recordings and the recordings to come, from the Live TV screen and
    // the guide, while this user may record.
    RouteSpec recordings;
    recordings.name = QStringLiteral("recordings");
    recordings.page = page("RecordingsPage");
    navigator.registerRoute(recordings);

    // The previous user's collections, rows, Live TV and decoded images go
    // with them; the new user's are asked for.
    m_account.scope().add(
        QStringLiteral("library"), account::UserScope::Library, m_ui,
        [this] {
          m_collections.invalidate();
          m_liveTv.checkAvailable();
          m_recorder.checkAvailable();
        },
        [this] {
          m_collections.invalidate();
          m_home.resetSession();
          m_liveTv.clear();
          m_recorder.clear();
          m_images.clearMemory();
        });
    // Once every module has the new user (or has let the old one go).
    QObject::connect(&m_account, &account::Account::sessionBegan, &navigator,
                     [&navigator] { navigator.reset(QStringLiteral("home")); });
    QObject::connect(&m_account, &account::Account::sessionEnded, &navigator,
                     [&navigator] { navigator.reset(QStringLiteral("login")); });
    // Restored before the screens were installed: home at once.
    if (m_account.signedIn()) navigator.reset(QStringLiteral("home"));
    return true;
  }

} // namespace jfd::ui
