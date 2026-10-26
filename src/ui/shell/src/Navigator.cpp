#include "jfd/ui/Navigator.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#include <algorithm>

namespace jfd::ui {

  namespace {
    Q_LOGGING_CATEGORY(lcNavigator, "jfd.ui.navigator")
  } // namespace

  QString Navigator::Route::toString() const {
    return argument.isEmpty() ? name : name + QLatin1Char('/') + argument;
  }

  Navigator::Navigator(QObject* parent) : QObject(parent) { registerBuiltinRoutes(); }

  void Navigator::registerBuiltinRoutes() {
    const auto add = [this](const char* name, bool takesArgument, bool root, bool chrome) {
      RouteSpec spec;
      spec.name = QLatin1String(name);
      spec.takesArgument = takesArgument;
      spec.root = root;
      spec.chrome = chrome;
      registerRoute(spec);
    };
    const auto addListed = [this](const char* name, const char* icon, const char* text, int order) {
      RouteSpec spec;
      spec.name = QLatin1String(name);
      spec.hasSidebarEntry = true;
      spec.sidebar.icon = QLatin1String(icon);
      spec.sidebar.text = QString::fromUtf8(text);
      spec.sidebar.translationContext = "Sidebar";
      spec.sidebar.order = order;
      registerRoute(spec);
    };
    add("login", false, true, false);
    addListed("home", "home", QT_TRANSLATE_NOOP("Sidebar", "Home"), 100);
    m_routes[QStringLiteral("home")].root = true;
    add("library", true, false, true);
    add("item", true, false, true);
    add("collection", true, false, true);
    addListed("search", "search", QT_TRANSLATE_NOOP("Sidebar", "Search"), 200);
    add("player", false, false, false);
    addListed("group", "group", QT_TRANSLATE_NOOP("Sidebar", "Watch together"), 300);
    addListed("settings", "settings", QT_TRANSLATE_NOOP("Sidebar", "Settings"), 900);
  }

  void Navigator::registerRoute(const RouteSpec& spec) {
    if (spec.name.isEmpty() || spec.name.contains(QLatin1Char('/'))) {
      qCWarning(lcNavigator) << "invalid route name" << spec.name;
      return;
    }
    const auto existing = m_routes.constFind(spec.name);
    RouteSpec stored = spec;
    if (existing != m_routes.cend()) {
      // Registering a route again keeps the page registered for it.
      if (stored.page.isEmpty()) {
        stored.page = existing->page;
      }
    } else {
      m_registered.append(spec.name);
    }
    m_routes.insert(spec.name, stored);
    Q_EMIT sidebarChanged();
  }

  QStringList Navigator::routeNames() const { return m_registered; }

  Navigator::Route Navigator::parse(const QString& route) const {
    const qsizetype slash = route.indexOf(QLatin1Char('/'));
    Route parsed{slash < 0 ? route : route.left(slash), slash < 0 ? QString() : route.mid(slash + 1)};
    const auto spec = m_routes.constFind(parsed.name);
    if (spec == m_routes.cend() || spec->takesArgument == parsed.argument.isEmpty() ||
        parsed.argument.contains(QLatin1Char('/'))) {
      return {};
    }
    return parsed;
  }

  void Navigator::registerPage(const QString& name, const QUrl& page) {
    const auto spec = m_routes.find(name);
    if (spec == m_routes.end()) {
      qCWarning(lcNavigator) << "no route named" << name;
      return;
    }
    spec->page = page;
  }

  void Navigator::setRouteAvailable(const QString& name, bool available) {
    if (available == !m_unavailable.contains(name)) {
      return;
    }
    if (available) {
      m_unavailable.removeAll(name);
    } else {
      m_unavailable.append(name);
    }
    Q_EMIT sidebarChanged();
  }

  bool Navigator::routeAvailable(const QString& name) const {
    return m_routes.contains(name) && !m_unavailable.contains(name);
  }

  QVariantList Navigator::sidebarEntries() const {
    struct Listed {
      int order;
      qsizetype registered;
      const RouteSpec* spec;
    };
    QList<Listed> listed;
    for (qsizetype i = 0; i < m_registered.size(); ++i) {
      const RouteSpec& spec = *m_routes.constFind(m_registered[i]);
      if (spec.hasSidebarEntry && routeAvailable(spec.name)) {
        listed.append({spec.sidebar.order, i, &spec});
      }
    }
    std::stable_sort(listed.begin(), listed.end(), [](const Listed& a, const Listed& b) {
      return a.order != b.order ? a.order < b.order : a.registered < b.registered;
    });
    QVariantList entries;
    for (const Listed& entry : listed) {
      const auto& sidebar = entry.spec->sidebar;
      const QString text = sidebar.translationContext
                               ? QCoreApplication::translate(sidebar.translationContext, sidebar.text.toUtf8().constData())
                               : sidebar.text;
      entries.append(QVariantMap{{QStringLiteral("route"), entry.spec->name},
                                 {QStringLiteral("text"), text},
                                 {QStringLiteral("icon"), sidebar.icon},
                                 {QStringLiteral("order"), sidebar.order}});
    }
    return entries;
  }

  QUrl Navigator::pageFor(const QString& name) const {
    const auto spec = m_routes.constFind(name);
    return spec != m_routes.cend() && !spec->page.isEmpty() ? spec->page : m_placeholder;
  }

  QString Navigator::currentRoute() const {
    return m_stack.isEmpty() ? QString() : m_stack.last().toString();
  }

  QString Navigator::currentName() const { return m_stack.isEmpty() ? QString() : m_stack.last().name; }

  bool Navigator::chromeVisible() const {
    const auto spec = m_routes.constFind(currentName());
    return spec != m_routes.cend() && spec->chrome;
  }

  QStringList Navigator::stack() const {
    QStringList routes;
    for (const Route& route : m_stack) {
      routes.append(route.toString());
    }
    return routes;
  }

  bool Navigator::navigate(const QString& route) {
    const Route parsed = parse(route);
    if (parsed.name.isEmpty()) {
      qCWarning(lcNavigator) << "invalid route" << route;
      return false;
    }
    if (m_routes.value(parsed.name).root) {
      return reset(route);
    }
    if (currentRoute() == parsed.toString()) {
      return true;
    }
    // Going to a page already in the stack goes back to it instead of
    // stacking a second copy (home -> settings -> search -> settings).
    for (qsizetype i = 0; i < m_stack.size(); ++i) {
      if (m_stack[i].toString() == parsed.toString() && !m_routes.value(parsed.name).takesArgument) {
        while (m_stack.size() > i + 1) {
          m_stack.removeLast();
          emitChange(Operation::Pop);
        }
        return true;
      }
    }
    m_stack.append(parsed);
    emitChange(Operation::Push);
    return true;
  }

  bool Navigator::replace(const QString& route) {
    const Route parsed = parse(route);
    if (parsed.name.isEmpty()) {
      qCWarning(lcNavigator) << "invalid route" << route;
      return false;
    }
    if (m_stack.isEmpty()) {
      m_stack.append(parsed);
      emitChange(Operation::Reset);
      return true;
    }
    m_stack.last() = parsed;
    emitChange(Operation::Replace);
    return true;
  }

  bool Navigator::reset(const QString& route) {
    const Route parsed = parse(route);
    if (parsed.name.isEmpty()) {
      qCWarning(lcNavigator) << "invalid route" << route;
      return false;
    }
    if (m_stack.size() == 1 && m_stack.first().toString() == parsed.toString()) {
      return true;
    }
    m_stack = {parsed};
    emitChange(Operation::Reset);
    return true;
  }

  bool Navigator::back() {
    if (m_stack.size() < 2) {
      return false;
    }
    m_stack.removeLast();
    emitChange(Operation::Pop);
    return true;
  }

  void Navigator::replay() {
    for (qsizetype i = 0; i < m_stack.size(); ++i) {
      const Route& route = m_stack[i];
      Q_EMIT stackChanged(i == 0 ? Operation::Reset : Operation::Push, pageFor(route.name),
                          propertiesFor(route));
    }
  }

  QVariantMap Navigator::propertiesFor(const Route& route) const {
    return {
        {QStringLiteral("route"), route.toString()},
        {QStringLiteral("routeName"), route.name},
        {QStringLiteral("routeArgument"), route.argument},
    };
  }

  void Navigator::emitChange(Operation operation) {
    const Route& current = m_stack.last();
    Q_EMIT stackChanged(operation, pageFor(current.name), propertiesFor(current));
    Q_EMIT currentChanged();
  }

} // namespace jfd::ui
