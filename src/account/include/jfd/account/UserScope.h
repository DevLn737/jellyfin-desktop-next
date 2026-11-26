#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;

namespace jfd::account {

  // What belongs to the signed-in user, in one place: each module that keeps
  // per-user state (a queue, a cache, a list of rooms, a report in flight)
  // registers it here once, and the scope begins and ends all of them in a
  // fixed order. Begin runs from the lowest order up; end from the highest
  // down, so what was set up last goes first (the player stops before the
  // session's state is dropped, the library's caches go last).
  //
  // A part added while a session is on begins at once, so a module installed
  // after the saved session was restored needs no special case. A part whose
  // context object is gone is skipped, as a connection to it would be.
  class UserScope {
  public:
    // Orders of the app's modules; gaps leave room for new ones.
    enum Order : int {
      Library = 100,
      Session = 200,
      Downloads = 300,
      Group = 400,
      Music = 500,
      Player = 600
    };

    using Hook = std::function<void()>;

    // Both hooks may be empty. Names are for the log and the tests.
    void add(const QString& name, int order, QObject* context, Hook begin, Hook end);

    // Runs every part's begin hook; nothing when the session is already on.
    void begin();
    // Runs every part's end hook; nothing when no session is on.
    void end();

    bool active() const { return m_active; }
    // In begin order.
    QStringList names() const;

  private:
    struct Part {
      QString name;
      int order = 0;
      QPointer<QObject> context;
      bool hasContext = false;
      Hook begin;
      Hook end;
    };
    static void run(const Part& part, const Hook& hook);

    QList<Part> m_parts;
    bool m_active = false;
  };

} // namespace jfd::account
