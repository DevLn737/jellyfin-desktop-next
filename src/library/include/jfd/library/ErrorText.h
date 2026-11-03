#pragma once

#include <jfd/api/Result.h>

#include <QString>

namespace jfd::library {

  // A sentence for the screens about a failed request, in the interface
  // language (context "ErrorText"). The transport's own text (Qt's network
  // message, "HTTP 500", the server's detail) is English and technical: it
  // goes to the log, not to the user. Empty for a cancelled request.
  QString errorText(const api::ApiError& error);

} // namespace jfd::library
