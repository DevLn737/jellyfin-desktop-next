#pragma once

#include <jfd/core/SessionInput.h>

#include <QObject>

#include <functional>

namespace jfd::session {

  // The session's one input queue on the Qt side. post() may be called from
  // any thread; every input reaches the handler on this object's thread, in
  // the order posted, from the event loop (never from inside post()), so mpv
  // events, network replies, timers and user intents interleave the same way
  // whichever thread they came from.
  class InputQueue final : public QObject {
    Q_OBJECT

  public:
    using Handler = std::function<void(core::SessionInput)>;

    explicit InputQueue(Handler handler, QObject* parent = nullptr);

    void post(core::SessionInput input);

  private:
    Handler m_handler;
  };

} // namespace jfd::session
