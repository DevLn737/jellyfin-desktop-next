#pragma once

#include <jfd/session/RoomsModel.h>

#include <QJSEngine>
#include <QQmlEngine>
#include <qqmlintegration.h>

namespace jfd::ui {

  // The session's RoomsModel as the QML singleton RoomsSession, beside
  // GroupSession. The application creates the model with the group model
  // and sets it here before loading any page that imports Jfd.Ui.Group.
  struct RoomsSession {
    Q_GADGET
    QML_FOREIGN(jfd::session::RoomsModel)
    QML_NAMED_ELEMENT(RoomsSession)
    QML_SINGLETON

  public:
    static void setInstance(session::RoomsModel* model) { s_instance = model; }
    static session::RoomsModel* instance() { return s_instance; }

    static session::RoomsModel* create(QQmlEngine*, QJSEngine* engine) {
      Q_ASSERT(s_instance != nullptr);
      Q_ASSERT(engine->thread() == s_instance->thread());
      // The application owns the model; QML must not delete it.
      QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
      return s_instance;
    }

  private:
    inline static session::RoomsModel* s_instance = nullptr;
  };

} // namespace jfd::ui
