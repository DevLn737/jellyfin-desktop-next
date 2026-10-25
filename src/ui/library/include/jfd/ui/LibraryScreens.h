#pragma once

#include <jfd/api/ClientIdentity.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/library/CollectionIndex.h>
#include <jfd/library/HomeModel.h>
#include <jfd/library/ImageCache.h>
#include <jfd/library/LibraryUpdates.h>
#include <jfd/library/SearchModel.h>
#include <jfd/library/ServerStore.h>
#include <jfd/library/SignIn.h>
#include <jfd/platform/CredentialStore.h>
#include <jfd/ui/LibraryImageProvider.h>

#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>

class QQmlEngine;

namespace jfd::ui {

  class LibraryUi;
  class Navigator;

  // Everything behind the library screens: the server connection (client
  // and session socket, shared with the session and group modules), saved
  // servers and tokens, the image cache and texture budget, and the home
  // and search models. install() puts them in front of QML: the image
  // provider, the `Library` singleton, the pages, and the move between
  // the login and home pages on sign-in and sign-out.
  class LibraryScreens {
  public:
    struct Config {
      api::ClientIdentity identity;
      QString serversFile;
      // Empty: images are cached in memory only.
      QString imageDirectory;
      std::unique_ptr<platform::CredentialStore> tokens;
      qreal devicePixelRatio = 1.0;
      qint64 textureBytes = LibraryImageProvider::DefaultTextureBytes;
      // Each library's sort and filters, keyed by library id. Unset: kept
      // for the session only.
      std::function<QJsonObject()> readFilters;
      std::function<void(const QJsonObject&)> writeFilters;
    };

    explicit LibraryScreens(Config config);
    ~LibraryScreens();

    // Before the window loads. Returns false when the engine has no
    // Jfd.Ui.Library module.
    bool install(QQmlEngine& engine, Navigator& navigator);

    api::JellyfinClient& client() { return m_client; }
    api::SessionSocket& socket() { return m_socket; }
    library::SignIn& signIn() { return m_signIn; }
    library::HomeModel& home() { return m_home; }
    library::CollectionIndex& collections() { return m_collections; }
    library::ImageCache& images() { return m_images; }
    const TextureBudget& textures() const { return m_textures; }
    // Null before install().
    LibraryUi* ui() const { return m_ui; }

  private:
    qreal m_devicePixelRatio;
    std::unique_ptr<platform::CredentialStore> m_tokens;
    library::ServerStore m_servers;
    api::JellyfinClient m_client;
    api::SessionSocket m_socket;
    library::LibraryUpdates m_updates;
    library::ImageCache m_images;
    library::SignIn m_signIn;
    library::HomeModel m_home;
    library::SearchModel m_search;
    library::CollectionIndex m_collections;
    std::function<QJsonObject()> m_readFilters;
    std::function<void(const QJsonObject&)> m_writeFilters;
    TextureBudget m_textures;
    LibraryUi* m_ui = nullptr;
  };

} // namespace jfd::ui
