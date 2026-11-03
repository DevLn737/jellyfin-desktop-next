#include "jfd/library/ErrorText.h"

#include <QCoreApplication>
#include <QLoggingCategory>

namespace jfd::library {

  namespace {
    Q_LOGGING_CATEGORY(lcErrors, "jfd.library.errors")

    QString text(const char* source) { return QCoreApplication::translate("ErrorText", source); }
  } // namespace

  QString errorText(const api::ApiError& error) {
    using Kind = api::ApiError::Kind;
    if (error.kind == Kind::Canceled) return {};
    qCWarning(lcErrors).noquote() << "request failed:" << error.message;
    switch (error.kind) {
    case Kind::Network:
      return text(QT_TRANSLATE_NOOP("ErrorText", "The server cannot be reached. Check the network connection."));
    case Kind::Timeout:
      return text(QT_TRANSLATE_NOOP("ErrorText", "The server did not answer in time."));
    case Kind::InvalidResponse:
      return text(QT_TRANSLATE_NOOP("ErrorText", "The server sent an answer the app does not understand."));
    case Kind::Unsupported:
      return text(QT_TRANSLATE_NOOP("ErrorText", "The server does not support this."));
    case Kind::Http:
      if (error.httpStatus == 401)
        return text(QT_TRANSLATE_NOOP("ErrorText", "The session has ended. Sign in again."));
      if (error.httpStatus == 403)
        return text(QT_TRANSLATE_NOOP("ErrorText", "The server does not allow this."));
      if (error.httpStatus == 404)
        return text(QT_TRANSLATE_NOOP("ErrorText", "The server could not find this."));
      if (error.httpStatus >= 500)
        return text(QT_TRANSLATE_NOOP("ErrorText", "The server ran into a problem (%1).")).arg(error.httpStatus);
      return text(QT_TRANSLATE_NOOP("ErrorText", "The server refused the request (%1).")).arg(error.httpStatus);
    case Kind::Canceled:
      break;
    }
    return {};
  }

} // namespace jfd::library
