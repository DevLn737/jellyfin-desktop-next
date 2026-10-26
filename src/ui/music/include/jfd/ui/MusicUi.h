#pragma once

#include <jfd/music/MusicPlayer.h>
#include <jfd/ui/MusicItem.h>
#include <jfd/ui/MusicListModel.h>

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <qqmlintegration.h>

namespace jfd::api {
  class JellyfinClient;
}
namespace jfd::music {
  class MusicPlayer;
}

namespace jfd::ui {

  // The music screens' way into the models, as the QML singleton `Music`:
  // whether the server has a music library, the lists the pages show, and
  // how times read. The player itself is the singleton `MusicSession`.
  class MusicUi : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // The music library's id when there is exactly one; empty lists the
    // music of every library.
    Q_PROPERTY(QString libraryId READ libraryId NOTIFY availableChanged)

  public:
    struct Config {
      api::JellyfinClient* client = nullptr;
      music::MusicPlayer* player = nullptr;
      qreal devicePixelRatio = 1.0;
    };

    explicit MusicUi(Config config, QObject* parent = nullptr);

    bool available() const { return m_available; }
    QString libraryId() const { return m_libraryId; }

    // Asks the server for the user's libraries; the screens show once one
    // of them is music. Done at sign-in.
    void refresh();

    // A list for a page; the page owns it (it goes with the page).
    Q_INVOKABLE jfd::ui::MusicListModel* createList(const QString& kind, QObject* owner);
    // The item of an album, artist or playlist page.
    Q_INVOKABLE jfd::ui::MusicItem* createItem(const QString& itemId, QObject* owner);
    // A cover (a track's album, an item's Primary image) at `width` logical
    // pixels; empty without a tag.
    Q_INVOKABLE QUrl coverUrl(const QString& itemId, const QString& tag, int width) const;
    // "3:07", or "1:02:03" from an hour on.
    Q_INVOKABLE QString formatDuration(double seconds) const;
    // "42 min", "1 h 5 min": an album's or playlist's length.
    Q_INVOKABLE QString formatTotal(double seconds) const;

  Q_SIGNALS:
    void availableChanged();

  private:
    Config m_config;
    bool m_available = false;
    QString m_libraryId;
    quint64 m_serial = 0;
  };

  // MusicUi and the MusicPlayer as QML singletons. Set before loading any
  // page that imports Jfd.Ui.Music.
  struct Music {
    Q_GADGET
    QML_FOREIGN(jfd::ui::MusicUi)
    QML_NAMED_ELEMENT(Music)
    QML_SINGLETON

  public:
    static void setInstance(MusicUi* ui) { s_instance = ui; }
    static MusicUi* instance() { return s_instance; }
    static MusicUi* create(QQmlEngine*, QJSEngine* engine) {
      Q_ASSERT(s_instance != nullptr);
      Q_ASSERT(engine->thread() == s_instance->thread());
      QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
      return s_instance;
    }

  private:
    inline static MusicUi* s_instance = nullptr;
  };

} // namespace jfd::ui

namespace jfd::music {
  class MusicPlayer;
}

namespace jfd::ui {

  struct MusicSession {
    Q_GADGET
    QML_FOREIGN(jfd::music::MusicPlayer)
    QML_NAMED_ELEMENT(MusicSession)
    QML_SINGLETON

  public:
    static void setInstance(music::MusicPlayer* player) { s_instance = player; }
    static music::MusicPlayer* instance() { return s_instance; }
    static music::MusicPlayer* create(QQmlEngine*, QJSEngine* engine);

  private:
    inline static music::MusicPlayer* s_instance = nullptr;
  };

} // namespace jfd::ui
