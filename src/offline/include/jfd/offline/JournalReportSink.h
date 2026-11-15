#pragma once

#include <jfd/offline/ViewJournal.h>
#include <jfd/playback/PlaybackReporter.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

namespace jfd::api {
  class JellyfinClient;
}

// The playback reports of the player and the music player, with the
// offline journal behind them (stage 3.10a). While the server can be
// reached, reports go out as usual (Sessions/Playing, .../Progress,
// .../Stopped; a downloaded copy reports DirectPlay without a
// PlaySessionId, like any direct play) and only a report that fails is
// written to the journal. Without the server every report is written to
// the journal at once. OfflineSync sends the journal when the server is
// back.
namespace jfd::offline {

  class JournalReportSink final : public playback::ReportSink {
  public:
    struct Hooks {
      // The session socket is Ready.
      std::function<bool()> online;
      // The signed-in server and user.
      std::function<QString()> serverId;
      std::function<QString()> userId;
      // Server minus system clock (ms) from the current TimeSync
      // measurement; nullopt while there is none. The journal keeps the
      // last one for later.
      std::function<std::optional<std::int64_t>()> clockOffsetMs;
      // System clock, ms since the epoch (tests set it).
      std::function<std::int64_t()> systemNowMs;
    };

    JournalReportSink(api::JellyfinClient& client, ViewJournal& journal, Hooks hooks);
    ~JournalReportSink() override;

    void started(const api::PlaybackProgress& progress) override;
    void progress(const api::PlaybackProgress& progress) override;
    void stopped(const api::PlaybackProgress& progress, bool failed,
                 std::function<void()> answered) override;

    // The playback the reports belong to now (tests).
    const QString& playId() const { return m_playId; }

  private:
    void record(const api::PlaybackProgress& progress, bool stopped);
    void sent(const QString& playId);
    // Server time now and the offset used.
    std::int64_t serverNowMs(std::int64_t& offset, bool& known) const;

    api::JellyfinClient& m_client;
    ViewJournal& m_journal;
    Hooks m_hooks;
    // Callbacks run only while it lives.
    QObject* m_context;
    QString m_playId;
  };

} // namespace jfd::offline
