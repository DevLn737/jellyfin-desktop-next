#include "jfd/ui/Navigator.h"

#include <QLoggingCategory>

namespace jfd::ui {

  namespace {
    Q_LOGGING_CATEGORY(lcNavigator, "jfd.ui.navigator")

    // Routes that take an argument ("item/<id>"); the others take none.
    bool takesArgument(const QString& name) {
      return name == QLatin1String("library") || name == QLatin1String("collection") ||
             name == QLatin1String("item");
    }

    bool isRoot(const QString& name) {
      return name == QLatin1String("login") || name == QLatin1String("home");
    }
  } // namespace

  QString Navigator::Route::toString() const {
    return argument.isEmpty() ? name : name + QLatin1Char('/') + argument;
  }

  Navigator::Navigator(QObject* parent) : QObject(parent) {}

  const QStringList& Navigator::routeNames() {
    static const QStringList names = {
        QStringLiteral("login"),  QStringLiteral("home"),       QStringLiteral("library"),
        QStringLiteral("item"),   QStringLiteral("collection"), QStringLiteral("search"),
        QStringLiteral("player"), QStringLiteral("group"),      QStringLiteral("settings"),
    };
    return names;
  }

  Navigator::Route Navigator::parse(const QString& route) {
    const qsizetype slash = route.indexOf(QLatin1Char('/'));
    Route parsed{slash < 0 ? route : route.left(slash), slash < 0 ? QString() : route.mid(slash + 1)};
    if (!routeNames().contains(parsed.name) || takesArgument(parsed.name) == parsed.argument.isEmpty() ||
        parsed.argument.contains(QLatin1Char('/'))) {
      return {};
    }
    return parsed;
  }

  void Navigator::registerPage(const QString& name, const QUrl& page) {
    if (!routeNames().contains(name)) {
      qCWarning(lcNavigator) << "no route named" << name;
      return;
    }
    m_pages.insert(name, page);
  }

  QUrl Navigator::pageFor(const QString& name) const { return m_pages.value(name, m_placeholder); }

  QString Navigator::currentRoute() const {
    return m_stack.isEmpty() ? QString() : m_stack.last().toString();
  }

  QString Navigator::currentName() const { return m_stack.isEmpty() ? QString() : m_stack.last().name; }

  bool Navigator::chromeVisible() const {
    const QString name = currentName();
    return !name.isEmpty() && name != QLatin1String("login") && name != QLatin1String("player");
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
    if (isRoot(parsed.name)) {
      return reset(route);
    }
    if (currentRoute() == parsed.toString()) {
      return true;
    }
    // Going to a page already in the stack goes back to it instead of
    // stacking a second copy (home -> settings -> search -> settings).
    for (qsizetype i = 0; i < m_stack.size(); ++i) {
      if (m_stack[i].toString() == parsed.toString() && parsed.name != QLatin1String("item") &&
          parsed.name != QLatin1String("library") && parsed.name != QLatin1String("collection")) {
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
