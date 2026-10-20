#pragma once

#include <jfd/core/MediaBackend.h>
#include <jfd/core/SessionInput.h>

#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

namespace jfd::testing {

  // A media backend without decoding: the file plays on the clock, opens and
  // seeks after short delays and reports progress every 250 ms, as mpv does.
  // For tests of the session wiring that need no video.
  class VirtualBackend final : public QObject, public core::MediaBackend {
  public:
    using Post = std::function<void(core::SessionInput)>;

    VirtualBackend(const core::Clock& clock, Post post) : m_clock(clock), m_post(std::move(post)) {
      m_tick.setInterval(250);
      QObject::connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_started) m_post(core::backend::Progress{m_epoch, sample()});
      });
      m_tick.start();
    }

    std::vector<std::string> loadedUrls;
    std::vector<core::ExternalTrack> addedTracks;

    void load(core::MediaEpoch epoch, const core::BackendLoad& request) override {
      m_epoch = epoch;
      m_started = false;
      m_paused = request.startPaused;
      m_position = 0.0;
      loadedUrls.push_back(request.url);
      later(80, [this, epoch] { m_post(core::backend::SourceOpened{epoch, 0.0, 600.0}); });
    }
    void start(core::MediaEpoch epoch, std::optional<core::BackendSeconds> position) override {
      later(60, [this, epoch, position] {
        m_position = position ? position->value : 0.0;
        m_anchor = m_clock.now();
        m_started = true;
        m_post(core::backend::PlaybackRestarted{epoch, sample()});
      });
    }
    void seek(core::MediaEpoch epoch, core::RequestId, core::BackendSeconds target) override {
      later(5, [this, epoch, target] {
        m_post(core::backend::SeekStarted{epoch});
        later(60, [this, epoch, target] {
          m_position = target.value;
          m_anchor = m_clock.now();
          m_post(core::backend::PlaybackRestarted{epoch, sample()});
        });
      });
    }
    void setPaused(core::MediaEpoch epoch, bool paused) override {
      m_position = current();
      m_anchor = m_clock.now();
      m_paused = paused;
      later(5, [this, epoch] { m_post(core::backend::Progress{epoch, sample()}); });
    }
    void setRate(core::MediaEpoch, core::PlaybackRate rate) override {
      m_position = current();
      m_anchor = m_clock.now();
      m_rate = rate.value();
    }
    void selectTrack(core::MediaEpoch, core::TrackKind, std::optional<int>) override {}
    void addExternalTrack(core::MediaEpoch epoch, core::RequestId request,
                          const core::ExternalTrack& track) override {
      addedTracks.push_back(track);
      later(50, [this, epoch, request] {
        m_post(core::backend::TrackAdded{epoch, request, static_cast<int>(addedTracks.size()) + 10});
      });
    }
    void stop(core::MediaEpoch epoch) override {
      m_started = false;
      later(5, [this, epoch] { m_post(core::backend::Ended{epoch, core::backend::EndReason::Stopped, {}}); });
    }

  private:
    // Delayed work for the file that is current now; dropped once replaced.
    void later(int ms, std::function<void()> work) {
      const core::MediaEpoch epoch = m_epoch;
      QTimer::singleShot(ms, this, [this, epoch, work = std::move(work)] {
        if (epoch == m_epoch) work();
      });
    }
    double current() const {
      if (!m_started || m_paused) return m_position;
      return m_position + std::chrono::duration<double>(m_clock.now() - m_anchor).count() * m_rate;
    }
    core::backend::Sample sample() const {
      return core::backend::Sample{core::BackendSeconds{current()}, m_paused, false, false, m_clock.now()};
    }

    const core::Clock& m_clock;
    Post m_post;
    QTimer m_tick;
    core::MediaEpoch m_epoch;
    bool m_started = false;
    bool m_paused = true;
    double m_position = 0.0;
    double m_rate = 1.0;
    core::LocalInstant m_anchor;
  };

} // namespace jfd::testing
