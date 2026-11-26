#pragma once

#include <jfd/account/Account.h>
#include <jfd/library/CollectionIndex.h>
#include <jfd/library/HomeModel.h>
#include <jfd/library/ImageCache.h>
#include <jfd/library/LibraryUpdates.h>
#include <jfd/library/LiveTvChannels.h>
#include <jfd/library/LiveTvOnNow.h>
#include <jfd/library/LiveTvRecorder.h>
#include <jfd/library/SearchModel.h>
#include <jfd/ui/LibraryImageProvider.h>

#include <QJsonObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>

#include <functional>
#include <memory>

namespace jfd::ui {

  class LibraryUi;
  class Navigator;

  // Everything behind the library screens on the account's connection: the
  // image cache and texture budget, the home, search and Live TV models.
  // install() puts them in front of QML: the image provider, the `Library`
  // singleton, the pages, and the move between the login and home pages
  // when the account's session begins and ends. Its per-user state is part
  // of the account's user scope (UserScope::Library).
  //
  // Destroy the window and the engine first, then this, then the account:
  // the pages' models hold the client by reference (src/desktop/main.cpp
  // does so). When this goes first anyway, its destructor detaches the
  // `Library` singleton and removes the image provider, so QML and the
  // image loader thread reach nothing that is gone through them.
  class LibraryScreens {
  public:
    struct Config {
      // Empty: images are cached in memory only.
      QString imageDirectory;
      qreal devicePixelRatio = 1.0;
      qint64 textureBytes = LibraryImageProvider::DefaultTextureBytes;
      // Each library's sort and filters, keyed by library id. Unset: kept
      // for the session only.
      std::function<QJsonObject()> readFilters;
      std::function<void(const QJsonObject&)> writeFilters;
    };

    LibraryScreens(account::Account& account, Config config);
    ~LibraryScreens();

    // Before the window loads. Returns false when the engine has no
    // Jfd.Ui.Library module.
    bool install(QQmlEngine& engine, Navigator& navigator);

    api::JellyfinClient& client() { return m_account.client(); }
    library::HomeModel& home() { return m_home; }
    library::LiveTvChannels& liveTv() { return m_liveTv; }
    // The home screen's "On now" row (section "livetv.onNow"), there while
    // Live TV is.
    library::LiveTvOnNow& onNow() { return m_onNow; }
    // Recording and the recordings to come (route "recordings"), there
    // when this user may record.
    library::LiveTvRecorder& recorder() { return m_recorder; }
    library::CollectionIndex& collections() { return m_collections; }
    library::ImageCache& images() { return m_images; }
    const TextureBudget& textures() const { return *m_textures; }
    // Null before install().
    LibraryUi* ui() const { return m_ui.data(); }

  private:
    void syncOnNow();

    account::Account& m_account;
    qreal m_devicePixelRatio;
    library::LibraryUpdates m_updates;
    library::ImageCache m_images;
    library::HomeModel m_home;
    library::SearchModel m_search;
    library::LiveTvChannels m_liveTv;
    library::LiveTvOnNow m_onNow;
    library::LiveTvRecorder m_recorder;
    library::CollectionIndex m_collections;
    std::function<QJsonObject()> m_readFilters;
    std::function<void(const QJsonObject&)> m_writeFilters;
    std::shared_ptr<TextureBudget> m_textures;
    // Both owned by the engine, which may go first.
    QPointer<LibraryUi> m_ui;
    QPointer<QQmlEngine> m_engine;
  };

} // namespace jfd::ui
