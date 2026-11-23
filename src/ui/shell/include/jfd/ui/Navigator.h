#pragma once

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <qqmlintegration.h>

namespace jfd::ui {

  // What a screen module says about a route when it registers it.
  struct RouteSpec {
    // Where a navigation entry shows: in the top bar, in the account menu,
    // or as a tab of the Library page.
    enum class Placement { Primary, AccountMenu, LibraryTab };

    // A navigation entry: where the route is listed.
    struct SidebarEntry {
      // A name in Jfd.Ui.Controls.Icons ("home") or an icon file URL.
      QString icon;
      // Shown text. With a translation context it is the source text and
      // is translated when the sidebar asks, so the language can be set
      // after the route is registered.
      QString text;
      const char* translationContext = nullptr;
      // Ascending; ties keep the order of registration.
      int order = 0;
      Placement placement = Placement::Primary;
      // A shorter text for the top bar's pill (same translation context);
      // empty: `text`.
      QString shortText = {};
    };

    QString name;
    // "album/<id>": the route needs an argument (and may be stacked many
    // times); the others take none and are returned to, not repeated.
    bool takesArgument = false;
    // "library" and "library/<id>": the argument may be left out. Such a
    // route is never stacked twice: going to it returns to the page in the
    // stack, and a different argument replaces that page's entry (the
    // Library page's tabs do not add history).
    bool optionalArgument = false;
    // Going there clears the stack (login, home).
    bool root = false;
    // The shell shows its sidebar and mini-player; false on login and in
    // the player.
    bool chrome = true;
    QUrl page;
    bool hasSidebarEntry = false;
    SidebarEntry sidebar;
  };

  // Page navigation by route. A route is a name with an optional argument:
  // "login", "home", "library" or "library/<id>", "item/<id>", "search", "player",
  // "group", "settings". Screen modules register a RouteSpec for a name
  // (a page, whether it takes an argument, a sidebar entry); names without
  // a page show a placeholder, so navigation works before every screen
  // exists. The built-in routes are registered by the constructor; other
  // packages add theirs ("music", "album/<id>", "requests").
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
    // The navigation entries, in order, of routes that are available: maps
    // with "route", "text", "icon", "order" and "placement" ("primary",
    // "account" or "libraryTab").
    Q_PROPERTY(QVariantList navEntries READ navEntries NOTIFY sidebarChanged)
    // navEntries without the Library entry, for the sidebar (which lists
    // the libraries themselves).
    Q_PROPERTY(QVariantList sidebarEntries READ sidebarEntries NOTIFY sidebarChanged)

  public:
    // Rebuild: the stack was trimmed; the view starts again from pages(),
    // creating only the top page (the others when Back reaches them).
    enum class Operation { Push, Pop, Replace, Reset, Rebuild };
    Q_ENUM(Operation)

    // The most pages kept: a push past it drops the oldest pages above the
    // first (home) down to TrimmedDepth, so an evening of item -> similar ->
    // person -> item browsing does not keep a hundred live pages with their
    // models and pictures. Back still walks the newest TrimmedDepth.
    static constexpr int MaxDepth = 20;
    static constexpr int TrimmedDepth = 12;

    struct Route {
      QString name;
      QString argument;
      QString toString() const;
    };

    explicit Navigator(QObject* parent = nullptr);

    // Registers a route, or replaces the spec of one with that name.
    void registerRoute(const RouteSpec& spec);
    // Route names this navigator accepts, in registration order.
    QStringList routeNames() const;
    bool hasRoute(const QString& name) const { return m_routes.contains(name); }
    // An empty name when the text is not a registered route with the
    // right argument.
    Route parse(const QString& route) const;

    // The page component for a route name; replaces an earlier page.
    void registerPage(const QString& name, const QUrl& page);
    // A route whose sidebar entry shows only while it is available (for
    // example "requests" once an address is set). Routes are available by
    // default.
    Q_INVOKABLE void setRouteAvailable(const QString& name, bool available);
    bool routeAvailable(const QString& name) const;
    // A route the user may not open at all (watching together without the
    // right to: SyncPlayAccess None): unavailable as above, and navigate(),
    // replace() and reset() refuse it, whoever asks (the sidebar, Ctrl+K,
    // a command from another start, the phone). A page of it in the stack
    // is left for home.
    void setRouteClosed(const QString& name, bool closed);
    bool routeClosed(const QString& name) const { return m_closed.contains(name); }
    QVariantList navEntries() const;
    QVariantList sidebarEntries() const;
    Q_INVOKABLE QUrl pageFor(const QString& name) const;
    // Shown for names without a registered page.
    void setPlaceholderPage(const QUrl& page) { m_placeholder = page; }

    QString currentRoute() const;
    QString currentName() const;
    int depth() const { return int(m_stack.size()); }
    bool canGoBack() const { return m_stack.size() > 1; }
    bool chromeVisible() const;
    QStringList stack() const;
    // Every page of the stack, bottom first, as StackView.push() takes an
    // array: page URL, then its properties, for each.
    Q_INVOKABLE QVariantList pages() const;

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
    void sidebarChanged();

  private:
    void registerBuiltinRoutes();
    QVariantMap propertiesFor(const Route& route) const;
    // parse(), and a warning for invalid and closed routes.
    Route accept(const QString& route) const;
    void emitChange(Operation operation);

    QList<Route> m_stack;
    QHash<QString, RouteSpec> m_routes;
    // Registration order, for ties in the sidebar and routeNames().
    QStringList m_registered;
    QStringList m_unavailable;
    QStringList m_closed;
    QUrl m_placeholder;
  };

} // namespace jfd::ui
