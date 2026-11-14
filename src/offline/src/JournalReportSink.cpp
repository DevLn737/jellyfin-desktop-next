#include <jfd/offline/JournalReportSink.h>

#include <jfd/api/JellyfinClient.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QUuid>

namespace jfd::offline {

  namespace {
    Q_LOGGING_CATEGORY(lcJournal, "jfd.offline.journal")
  } // namespace

  JournalReportSink::JournalReportSink(api::JellyfinClient& client, ViewJournal& journal, Hooks hooks)
      : m_client(client), m_journal(journal), m_hooks(std::move(hooks)), m_context(new QObject) {
    if (!m_hooks.systemNowMs) m_hooks.systemNowMs = [] { return QDateTime::currentMSecsSinceEpoch(); };
  }

  JournalReportSink::~JournalReportSink() { delete m_context; }

  std::int64_t JournalReportSink::serverNowMs(std::int64_t& offset, bool& known) const {
    std::optional<std::int64_t> measured = m_hooks.clockOffsetMs ? m_hooks.clockOffsetMs() : std::nullopt;
    if (measured) m_journal.setClockOffsetMs(*measured);
    const std::optional<std::int64_t> last = measured ? measured : m_journal.clockOffsetMs();
    known = last.has_value();
    offset = last.value_or(0);
    return m_hooks.systemNowMs() + offset;
  }

  void JournalReportSink::record(const api::PlaybackProgress& progress, bool stopped) {
    ViewEvent event;
    event.serverId = m_hooks.serverId ? m_hooks.serverId() : QString();
    event.userId = m_hooks.userId ? m_hooks.userId() : QString();
    event.itemId = progress.itemId;
    event.playId = m_playId;
    event.positionTicks = progress.positionTicks;
    event.stopped = stopped;
    event.atMs = serverNowMs(event.clockOffsetMs, event.clockKnown);
    // A live channel has no position to keep.
    if (event.serverId.isEmpty() || event.userId.isEmpty() || event.itemId.isEmpty() ||
        !progress.liveStreamId.isEmpty())
      return;
    if (!m_journal.append(event)) qCWarning(lcJournal) << "cannot write the offline journal";
  }

  void JournalReportSink::sent(const QString& playId) {
    // A later report of the playback reached the server: what failed before
    // is out of date.
    if (m_journal.hasPlay(playId)) m_journal.removePlay(playId);
  }

  void JournalReportSink::started(const api::PlaybackProgress& progress) {
    m_playId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_hooks.online && !m_hooks.online()) {
      record(progress, false);
      return;
    }
    const QString playId = m_playId;
    m_client.reportPlaybackStart(progress, m_context,
                                 [this, progress, playId](api::Result<api::Empty> result) {
                                   if (result) return sent(playId);
                                   qCInfo(lcJournal) << "start report failed:" << result.error().message;
                                   if (playId != m_playId) return;
                                   record(progress, false);
                                 });
  }

  void JournalReportSink::progress(const api::PlaybackProgress& progress) {
    if (m_playId.isEmpty()) m_playId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_hooks.online && !m_hooks.online()) {
      record(progress, false);
      return;
    }
    const QString playId = m_playId;
    m_client.reportPlaybackProgress(progress, m_context,
                                    [this, progress, playId](api::Result<api::Empty> result) {
                                      if (result) return sent(playId);
                                      if (playId != m_playId) return;
                                      record(progress, false);
                                    });
  }

  void JournalReportSink::stopped(const api::PlaybackProgress& progress, bool failed,
                                  std::function<void()> answered) {
    if (m_playId.isEmpty()) m_playId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_hooks.online && !m_hooks.online()) {
      record(progress, true);
      if (answered) answered();
      return;
    }
    const QString playId = m_playId;
    m_client.reportPlaybackStopped(
        progress, failed, m_context,
        [this, progress, playId, answered = std::move(answered)](api::Result<api::Empty> result) {
          if (result) {
            sent(playId);
          } else {
            qCInfo(lcJournal) << "stop report failed:" << result.error().message;
            // Written with the playback's own id, even if another started.
            const QString current = m_playId;
            m_playId = playId;
            record(progress, true);
            m_playId = current;
          }
          if (answered) answered();
        });
  }

} // namespace jfd::offline
