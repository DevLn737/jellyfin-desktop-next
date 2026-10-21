#pragma once

#include <jfd/ui/PlayerModel.h>

#include <QJSEngine>
#include <QQmlEngine>
#include <qqmlintegration.h>

namespace jfd::ui {

  // The application's PlayerModel as the QML singleton PlayerSession. Set
  // before loading any page that imports Jfd.Ui.Player.
  struct PlayerSession {
    Q_GADGET
    QML_FOREIGN(jfd::ui::PlayerModel)
    QML_NAMED_ELEMENT(PlayerSession)
    QML_SINGLETON

  public:
    static void setInstance(PlayerModel* model) { s_instance = model; }
    static PlayerModel* instance() { return s_instance; }

    static PlayerModel* create(QQmlEngine*, QJSEngine* engine) {
      Q_ASSERT(s_instance != nullptr);
      Q_ASSERT(engine->thread() == s_instance->thread());
      QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
      return s_instance;
    }

  private:
    inline static PlayerModel* s_instance = nullptr;
  };

} // namespace jfd::ui
