#include <jfd/platform/MediaControls.h>

#include <QMetaObject>
#include <QPointer>
#include <QWindow>

#include <windows.h>

#include <systemmediatransportcontrolsinterop.h>

#pragma warning(push)
#pragma warning(disable : 4265 4946 5205)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma warning(pop)

#include <chrono>

// System Media Transport Controls: the media overlay, the lock screen and the
// keyboard's media keys. SMTC calls back on its own threads; commands are
// queued to this object's thread.
namespace jfd::platform {

  namespace {
    namespace media = winrt::Windows::Media;
    using std::chrono::milliseconds;

    winrt::Windows::Foundation::TimeSpan span(qint64 ms) {
      return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(milliseconds(ms));
    }

    class SmtcMediaControls final : public MediaControls {
    public:
      using MediaControls::MediaControls;

      ~SmtcMediaControls() override { detach(); }

      bool isAvailable() const override { return static_cast<bool>(m_smtc); }

      void attachWindow(QWindow* window) override {
        detach();
        if (!window) return;
        try {
          winrt::init_apartment(winrt::apartment_type::single_threaded);
        } catch (const winrt::hresult_error&) {
          // Qt already initialised COM on this thread; that is fine.
        }
        try {
          auto interop = winrt::get_activation_factory<media::SystemMediaTransportControls,
                                                       ISystemMediaTransportControlsInterop>();
          media::SystemMediaTransportControls smtc{nullptr};
          winrt::check_hresult(interop->GetForWindow(reinterpret_cast<HWND>(window->winId()),
                                                     winrt::guid_of<media::SystemMediaTransportControls>(),
                                                     winrt::put_abi(smtc)));
          m_smtc = smtc;
          m_smtc.IsEnabled(true);
          QPointer<SmtcMediaControls> self(this);
          m_buttonToken = m_smtc.ButtonPressed(
              [self](const media::SystemMediaTransportControls&,
                     const media::SystemMediaTransportControlsButtonPressedEventArgs& args) {
                const auto button = args.Button();
                QMetaObject::invokeMethod(self, [self, button] {
                  if (self) self->pressed(button);
                }, Qt::QueuedConnection);
              });
          m_seekToken = m_smtc.PlaybackPositionChangeRequested(
              [self](const media::SystemMediaTransportControls&,
                     const media::PlaybackPositionChangeRequestedEventArgs& args) {
                const qint64 ms = std::chrono::duration_cast<milliseconds>(args.RequestedPlaybackPosition()).count();
                QMetaObject::invokeMethod(self, [self, ms] {
                  if (self && self->m_state.canSeek) Q_EMIT self->seekRequested(ms);
                }, Qt::QueuedConnection);
              });
          apply(true);
        } catch (const winrt::hresult_error& error) {
          qWarning("System media controls are not available: 0x%08x", static_cast<unsigned>(error.code()));
          m_smtc = nullptr;
        }
      }

      void setState(const MediaControlsState& state) override {
        const bool metadataChanged = state.title != m_state.title || state.subtitle != m_state.subtitle ||
                                     state.artUrl != m_state.artUrl;
        m_state = state;
        if (m_smtc) apply(metadataChanged);
      }

    private:
      void apply(bool metadata) {
        try {
          using Status = media::MediaPlaybackStatus;
          const bool stopped = m_state.status == MediaControlsState::Status::Stopped;
          m_smtc.PlaybackStatus(stopped ? Status::Stopped
                                        : m_state.status == MediaControlsState::Status::Playing ? Status::Playing
                                                                                                 : Status::Paused);
          m_smtc.IsPlayEnabled(!stopped);
          m_smtc.IsPauseEnabled(!stopped);
          m_smtc.IsStopEnabled(!stopped);
          m_smtc.IsNextEnabled(m_state.canGoNext);
          m_smtc.IsPreviousEnabled(m_state.canGoPrevious);
          m_smtc.PlaybackRate(m_state.rate);

          if (metadata) {
            auto updater = m_smtc.DisplayUpdater();
            updater.ClearAll();
            if (!stopped || !m_state.title.isEmpty()) {
              updater.Type(media::MediaPlaybackType::Video);
              updater.VideoProperties().Title(winrt::hstring(m_state.title.toStdWString()));
              updater.VideoProperties().Subtitle(winrt::hstring(m_state.subtitle.toStdWString()));
              if (m_state.artUrl.isValid()) {
                updater.Thumbnail(winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromUri(
                    winrt::Windows::Foundation::Uri(winrt::hstring(m_state.artUrl.toString().toStdWString()))));
              }
            }
            updater.Update();
          }

          media::SystemMediaTransportControlsTimelineProperties timeline;
          timeline.StartTime(span(0));
          timeline.MinSeekTime(span(0));
          timeline.EndTime(span(m_state.durationMs));
          timeline.MaxSeekTime(span(m_state.canSeek ? m_state.durationMs : 0));
          timeline.Position(span(m_state.positionMs));
          m_smtc.UpdateTimelineProperties(timeline);
        } catch (const winrt::hresult_error& error) {
          qWarning("Updating the system media controls failed: 0x%08x", static_cast<unsigned>(error.code()));
        }
      }

      void pressed(media::SystemMediaTransportControlsButton button) {
        using Button = media::SystemMediaTransportControlsButton;
        switch (button) {
        case Button::Play: Q_EMIT commandRequested(MediaCommand::Play); break;
        case Button::Pause: Q_EMIT commandRequested(MediaCommand::Pause); break;
        case Button::Stop: Q_EMIT commandRequested(MediaCommand::Stop); break;
        case Button::Next:
          if (m_state.canGoNext) Q_EMIT commandRequested(MediaCommand::Next);
          break;
        case Button::Previous:
          if (m_state.canGoPrevious) Q_EMIT commandRequested(MediaCommand::Previous);
          break;
        default: break;
        }
      }

      void detach() {
        if (!m_smtc) return;
        try {
          m_smtc.ButtonPressed(m_buttonToken);
          m_smtc.PlaybackPositionChangeRequested(m_seekToken);
          m_smtc.DisplayUpdater().ClearAll();
          m_smtc.DisplayUpdater().Update();
          m_smtc.IsEnabled(false);
        } catch (const winrt::hresult_error&) {
        }
        m_smtc = nullptr;
      }

      media::SystemMediaTransportControls m_smtc{nullptr};
      winrt::event_token m_buttonToken{};
      winrt::event_token m_seekToken{};
      MediaControlsState m_state;
    };
  } // namespace

  std::unique_ptr<MediaControls> createSystemMediaControls(const QString&, const QString&, QObject* parent) {
    return std::make_unique<SmtcMediaControls>(parent);
  }

} // namespace jfd::platform
