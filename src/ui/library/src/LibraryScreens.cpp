#include "jfd/ui/LibraryScreens.h"

#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

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

    QUrl page(const char* name) {
      return QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Library/%1.qml").arg(QLatin1String(name)));
    }
  } // namespace

  LibraryScreens::LibraryScreens(Config config)
      : m_devicePixelRatio(config.devicePixelRatio),
        m_tokens(config.tokens ? std::move(config.tokens)
                               : platform::createUnavailableCredentialStore()),
        m_servers(config.serversFile), m_client(config.identity), m_socket(m_client),
        m_images(m_client, config.imageDirectory), m_signIn(m_client, m_servers, *m_tokens),
        m_home(m_client, homeOptions(config.devicePixelRatio)),
        m_search(m_client, searchOptions(config.devicePixelRatio)), m_collections(m_client),
        m_readFilters(std::move(config.readFilters)),
        m_writeFilters(std::move(config.writeFilters)),
        m_textures(std::make_shared<TextureBudget>(config.textureBytes)) {
    m_updates.attach(m_socket);
    m_home.follow(m_updates);
    m_search.follow(m_updates);
    m_collections.follow(m_updates);
  }

  LibraryScreens::~LibraryScreens() = default;

  bool LibraryScreens::install(QQmlEngine& engine, Navigator& navigator) {
    m_ui = engine.singletonInstance<LibraryUi*>("Jfd.Ui.Library", "Library");
    if (m_ui == nullptr) return false;

    engine.addImageProvider(QLatin1String(LibraryImageProvider::Id),
                            new LibraryImageProvider(m_images, m_textures));

    LibraryUi::Services services;
    services.client = &m_client;
    services.signIn = &m_signIn;
    services.home = &m_home;
    services.search = &m_search;
    services.updates = &m_updates;
    services.collections = &m_collections;
    services.readFilters = m_readFilters;
    services.writeFilters = m_writeFilters;
    services.devicePixelRatio = m_devicePixelRatio;
    m_ui->attach(services);

    navigator.registerPage(QStringLiteral("login"), page("LoginPage"));
    navigator.registerPage(QStringLiteral("home"), page("HomePage"));
    navigator.registerPage(QStringLiteral("library"), page("LibraryPage"));
    navigator.registerPage(QStringLiteral("collection"), page("CollectionPage"));
    navigator.registerPage(QStringLiteral("item"), page("ItemPage"));
    navigator.registerPage(QStringLiteral("search"), page("SearchPage"));
    navigator.registerPage(QStringLiteral("customizeHome"), page("CustomizeHomePage"));

    QObject::connect(&m_signIn, &library::SignIn::signedIn, &navigator, [this, &navigator] {
      m_collections.invalidate();
      m_socket.start();
      navigator.reset(QStringLiteral("home"));
    });
    QObject::connect(&m_signIn, &library::SignIn::signedOut, &navigator, [this, &navigator] {
      m_socket.stop();
      m_collections.invalidate();
      m_home.resetSession();
      navigator.reset(QStringLiteral("login"));
    });
    return true;
  }

} // namespace jfd::ui
