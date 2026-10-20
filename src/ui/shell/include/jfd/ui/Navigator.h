#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <qqmlintegration.h>

namespace jfd::ui {

  // Page navigation by route. A route is a name with an optional argument:
  // "login", "home", "library/<id>", "item/<id>", "search", "player",
  // "group", "settings". Screen modules register the QML page for a name;
  // names without a page show a placeholder, so navigation works before
  // every screen exists.
  //
  // The shell's StackView follows the stack through stackChanged. The QML
  // singleton is created by the engine; the application registers pages on
  // it before loading the window.
  class Navigator : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString currentRoute READ currentRoute NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(int depth READ depth NOTIFY currentChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentChanged)
    // False on login and in the player: the shell hides its sidebar.
    Q_PROPERTY(bool chromeVisible READ chromeVisible NOTIFY currentChanged)

  public:
    enum class Operation { Push, Pop, Replace, Reset };
    Q_ENUM(Operation)

    struct Route {
      QString name;
      QString argument;
      QString toString() const;
    };

    explicit Navigator(QObject* parent = nullptr);

    // Route names this navigator accepts.
    static const QStringList& routeNames();
    // nullopt-like: an empty name when the text is not a valid route.
    static Route parse(const QString& route);

    // The page component for a route name; replaces an earlier page.
    void registerPage(const QString& name, const QUrl& page);
    Q_INVOKABLE QUrl pageFor(const QString& name) const;
    // Shown for names without a registered page.
    void setPlaceholderPage(const QUrl& page) { m_placeholder = page; }

    QString currentRoute() const;
    QString currentName() const;
    int depth() const { return int(m_stack.size()); }
    bool canGoBack() const { return m_stack.size() > 1; }
    bool chromeVisible() const;
    QStringList stack() const;

    // Opens a route on top of the current page. "login" and "home" are
    // roots: going there clears the stack. Going to the current route does
    // nothing. Returns false for an invalid route.
    Q_INVOKABLE bool navigate(const QString& route);
    // Replaces the current page (for example login -> home after sign-in).
    Q_INVOKABLE bool replace(const QString& route);
    // Clears the stack and opens the route.
    Q_INVOKABLE bool reset(const QString& route);
    // Returns false when already at the first page.
    Q_INVOKABLE bool back();
    // Emits the whole stack again (a Reset, then a Push per page), for a
    // view created after navigation started.
    Q_INVOKABLE void replay();

  Q_SIGNALS:
    // For the StackView: what to do, the page to show and its properties
    // ("route", "routeName", "routeArgument"). Pop carries the page that
    // becomes current.
    void stackChanged(jfd::ui::Navigator::Operation operation, const QUrl& page,
                      const QVariantMap& properties);
    void currentChanged();

  private:
    QVariantMap propertiesFor(const Route& route) const;
    void emitChange(Operation operation);

    QList<Route> m_stack;
    QHash<QString, QUrl> m_pages;
    QUrl m_placeholder;
  };

} // namespace jfd::ui
