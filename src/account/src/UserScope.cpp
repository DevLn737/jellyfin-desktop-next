#include <jfd/account/UserScope.h>

#include <QLoggingCategory>
#include <QObject>

#include <algorithm>

namespace jfd::account {

  namespace {
    Q_LOGGING_CATEGORY(lcScope, "jfd.account.scope")
  }

  void UserScope::add(const QString& name, int order, QObject* context, Hook begin, Hook end) {
    Part part;
    part.name = name;
    part.order = order;
    part.context = context;
    part.hasContext = context != nullptr;
    part.begin = std::move(begin);
    part.end = std::move(end);
    // After the parts of the same order that came before it.
    const auto at = std::upper_bound(m_parts.begin(), m_parts.end(), order,
                                     [](int value, const Part& other) { return value < other.order; });
    const auto inserted = m_parts.insert(at, std::move(part));
    if (m_active) run(*inserted, inserted->begin);
  }

  void UserScope::run(const Part& part, const Hook& hook) {
    if (!hook) return;
    if (part.hasContext && part.context.isNull()) return;
    hook();
  }

  void UserScope::begin() {
    if (m_active) return;
    m_active = true;
    qCDebug(lcScope) << "begin" << names();
    // A hook may add a part; the copy keeps the walk valid, and the new
    // part has begun in add().
    const QList<Part> parts = m_parts;
    for (const Part& part : parts)
      run(part, part.begin);
  }

  void UserScope::end() {
    if (!m_active) return;
    m_active = false;
    qCDebug(lcScope) << "end";
    const QList<Part> parts = m_parts;
    for (auto it = parts.crbegin(); it != parts.crend(); ++it)
      run(*it, it->end);
  }

  QStringList UserScope::names() const {
    QStringList result;
    for (const Part& part : m_parts)
      result.append(part.name);
    return result;
  }

} // namespace jfd::account
