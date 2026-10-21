#include "MpvPlayer.h"

#include <QDebug>
#include <QMetaObject>

#include <atomic>
#include <vector>

namespace jfd::media {

namespace {
enum PropertyId : std::uint64_t {
  Pause = 1,
  TimePos,
  Duration,
  HwdecCurrent,
  MediaTitle,
  Avsync,
};

QVariant nodeToVariant(const mpv_node& node)
{
  switch (node.format)
  {
  case MPV_FORMAT_STRING: return QString::fromUtf8(node.u.string);
  case MPV_FORMAT_FLAG: return node.u.flag != 0;
  case MPV_FORMAT_INT64: return static_cast<qlonglong>(node.u.int64);
  case MPV_FORMAT_DOUBLE: return node.u.double_;
  case MPV_FORMAT_NODE_ARRAY:
  {
    QVariantList list;
    for (int i = 0; i < node.u.list->num; ++i)
      list.append(nodeToVariant(node.u.list->values[i]));
    return list;
  }
  case MPV_FORMAT_NODE_MAP:
  {
    QVariantMap map;
    for (int i = 0; i < node.u.list->num; ++i)
      map.insert(QString::fromUtf8(node.u.list->keys[i]), nodeToVariant(node.u.list->values[i]));
    return map;
  }
  default: return {};
  }
}

// One pending wakeup at a time; mpv may call the callback from any thread.
struct Wakeup
{
  MpvPlayer* player;
  std::atomic<bool> pending {false};
};
}

MpvPlayer::MpvPlayer(const Options& options, QObject* parent)
  : QObject(parent), m_mpv(createMpv())
{
  if (!m_mpv)
    return;
  mpv_handle* mpv = m_mpv.get();
  const std::vector<std::pair<const char*, QByteArray>> settings {
    {"config", "no"},
    {"terminal", "no"},
    {"msg-level", "all=warn"},
    {"vo", "libmpv"},
    {"hwdec", options.hwdec.toUtf8()},
    {"hwdec-codecs", "all"},
    // The render API timing is driven by Qt's swap; mpv must not delay frames
    // by its own estimate of the display latency (ported from the 2.x client).
    {"video-timing-offset", "0"},
    {"keep-open", "yes"},
    {"idle", "yes"},
    {"osd-level", "0"},
    {"input-default-bindings", "no"},
    {"input-vo-keyboard", "no"},
    {"audio-fallback-to-null", "yes"},
    {"audio-client-name", "Jellyfin Desktop Next"},
    {"loop-file", options.loopFile ? "inf" : "no"},
  };
  for (const auto& [name, value] : settings)
    if (mpv_set_option_string(mpv, name, value.constData()) < 0)
      qWarning() << "mpv option rejected:" << name << value;
  if (!options.audioOutput.isEmpty())
    mpv_set_option_string(mpv, "ao", options.audioOutput.toUtf8().constData());
  for (const auto& [name, value] : options.extra)
    if (mpv_set_option_string(mpv, name.constData(), value.constData()) < 0)
      qWarning() << "mpv option rejected:" << name << value;

  if (mpv_initialize(mpv) < 0)
  {
    m_mpv.reset();
    return;
  }
  mpv_request_log_messages(mpv, "warn");
  mpv_observe_property(mpv, Pause, "pause", MPV_FORMAT_FLAG);
  mpv_observe_property(mpv, TimePos, "time-pos", MPV_FORMAT_DOUBLE);
  mpv_observe_property(mpv, Duration, "duration", MPV_FORMAT_DOUBLE);
  mpv_observe_property(mpv, HwdecCurrent, "hwdec-current", MPV_FORMAT_STRING);
  mpv_observe_property(mpv, MediaTitle, "media-title", MPV_FORMAT_STRING);
  mpv_observe_property(mpv, Avsync, "avsync", MPV_FORMAT_DOUBLE);

  auto* wakeup = new Wakeup {this};
  mpv_set_wakeup_callback(mpv, [](void* context) {
    auto* state = static_cast<Wakeup*>(context);
    if (state->pending.exchange(true))
      return;
    QMetaObject::invokeMethod(state->player, [state] {
      state->pending.store(false);
      state->player->drainEvents();
    }, Qt::QueuedConnection);
  }, wakeup);
  connect(this, &QObject::destroyed, [wakeup] { delete wakeup; });
}

MpvPlayer::~MpvPlayer()
{
  if (m_mpv)
  {
    // The core may outlive this object through the render context's copy.
    mpv_set_wakeup_callback(m_mpv.get(), nullptr, nullptr);
    command({"stop"});
  }
}

void MpvPlayer::command(std::initializer_list<const char*> arguments)
{
  if (!m_mpv)
    return;
  std::vector<const char*> list(arguments);
  list.push_back(nullptr);
  if (const int result = mpv_command_async(m_mpv.get(), 0, list.data()); result < 0)
    emit error(QString::fromUtf8(mpv_error_string(result)));
}

void MpvPlayer::load(const QString& url)
{
  const QByteArray bytes = url.toUtf8();
  m_awaitingFirstFrame = true;
  m_loadTimer.start();
  command({"loadfile", bytes.constData(), "replace"});
}

void MpvPlayer::seek(double seconds)
{
  const QByteArray target = QByteArray::number(seconds, 'f', 3);
  m_awaitingSeek = true;
  m_seekTimer.start();
  command({"seek", target.constData(), "absolute+exact"});
}

void MpvPlayer::seekRelative(double seconds)
{
  const QByteArray target = QByteArray::number(seconds, 'f', 3);
  m_awaitingSeek = true;
  m_seekTimer.start();
  command({"seek", target.constData(), "relative"});
}

void MpvPlayer::setPaused(bool paused)
{
  if (!m_mpv)
    return;
  int flag = paused ? 1 : 0;
  mpv_set_property_async(m_mpv.get(), 0, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::setDoubleProperty(const char* name, double value)
{
  if (!m_mpv)
    return;
  mpv_set_property_async(m_mpv.get(), 0, name, MPV_FORMAT_DOUBLE, &value);
}

void MpvPlayer::stop()
{
  command({"stop"});
}

QVariant MpvPlayer::property(const char* name) const
{
  if (!m_mpv)
    return {};
  mpv_node node;
  if (mpv_get_property(m_mpv.get(), name, MPV_FORMAT_NODE, &node) < 0)
    return {};
  QVariant result = nodeToVariant(node);
  mpv_free_node_contents(&node);
  return result;
}

void MpvPlayer::drainEvents()
{
  if (!m_mpv)
    return;
  for (;;)
  {
    const mpv_event* event = mpv_wait_event(m_mpv.get(), 0);
    switch (event->event_id)
    {
    case MPV_EVENT_NONE:
      if (m_listener)
        m_listener(*event);
      return;
    case MPV_EVENT_PROPERTY_CHANGE:
      handleProperty(*static_cast<const mpv_event_property*>(event->data));
      break;
    case MPV_EVENT_PLAYBACK_RESTART:
      if (m_awaitingFirstFrame)
      {
        m_awaitingFirstFrame = false;
        m_awaitingSeek = false;
        emit firstFrame(static_cast<double>(m_loadTimer.nsecsElapsed()) / 1e6);
      }
      else if (m_awaitingSeek)
      {
        m_awaitingSeek = false;
        emit seekCompleted(static_cast<double>(m_seekTimer.nsecsElapsed()) / 1e6);
      }
      break;
    case MPV_EVENT_END_FILE:
    {
      const auto* end = static_cast<const mpv_event_end_file*>(event->data);
      if (end->reason == MPV_END_FILE_REASON_ERROR)
        emit error(QString::fromUtf8(mpv_error_string(end->error)));
      emit endOfFile(end->reason == MPV_END_FILE_REASON_EOF ? QStringLiteral("eof") : QStringLiteral("other"));
      break;
    }
    case MPV_EVENT_CLIENT_MESSAGE:
    {
      const auto* message = static_cast<const mpv_event_client_message*>(event->data);
      QStringList arguments;
      for (int i = 0; i < message->num_args; ++i)
        arguments.append(QString::fromUtf8(message->args[i]));
      emit clientMessage(arguments);
      break;
    }
    case MPV_EVENT_LOG_MESSAGE:
    {
      const auto* message = static_cast<const mpv_event_log_message*>(event->data);
      qWarning().noquote() << "mpv" << message->prefix << QString::fromUtf8(message->text).trimmed();
      break;
    }
    default:
      break;
    }
    if (m_listener)
      m_listener(*event);
  }
}

void MpvPlayer::handleProperty(const mpv_event_property& property)
{
  const bool present = property.format != MPV_FORMAT_NONE && property.data;
  const QByteArray name(property.name);
  if (name == "pause")
  {
    const bool value = present && *static_cast<int*>(property.data) != 0;
    if (value != m_paused) { m_paused = value; emit pausedChanged(); }
  }
  else if (name == "time-pos")
  {
    m_position = present ? *static_cast<double*>(property.data) : 0;
    emit positionChanged();
  }
  else if (name == "duration")
  {
    m_duration = present ? *static_cast<double*>(property.data) : 0;
    emit durationChanged();
  }
  else if (name == "hwdec-current")
  {
    m_hwdecCurrent = present ? QString::fromUtf8(*static_cast<char**>(property.data)) : QString();
    emit hwdecCurrentChanged();
  }
  else if (name == "media-title")
  {
    m_title = present ? QString::fromUtf8(*static_cast<char**>(property.data)) : QString();
    emit titleChanged();
  }
  else if (name == "avsync")
  {
    m_avsync = present ? *static_cast<double*>(property.data) : 0;
  }
}

}
