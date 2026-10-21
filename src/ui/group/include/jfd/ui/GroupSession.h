#pragma once

#include <jfd/session/GroupModel.h>

#include <QJSEngine>
#include <QQmlEngine>
#include <qqmlintegration.h>

namespace jfd::ui {

  // The session's GroupModel as the QML singleton GroupSession. The
  // application creates the model with the session and sets it here before
  // loading any page that imports Jfd.Ui.Group.
  struct GroupSession {
    Q_GADGET
    QML_FOREIGN(jfd::session::GroupModel)
    QML_NAMED_ELEMENT(GroupSession)
    QML_SINGLETON

  public:
    static void setInstance(session::GroupModel* model) { s_instance = model; }
    static session::GroupModel* instance() { return s_instance; }

    static session::GroupModel* create(QQmlEngine*, QJSEngine* engine) {
      Q_ASSERT(s_instance != nullptr);
      Q_ASSERT(engine->thread() == s_instance->thread());
      // The application owns the model; QML must not delete it.
      QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
      return s_instance;
    }

  private:
    inline static session::GroupModel* s_instance = nullptr;
  };

} // namespace jfd::ui
