#include <jfd/platform/DBusServices.h>

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QStringList>
#include <QVariantMap>

#include <cmath>

// MPRIS 2.2 (https://specifications.freedesktop.org/mpris-spec/latest/): the
// root interface and the player interface on /org/mpris/MediaPlayer2. Times
// are in microseconds. Position is not announced in PropertiesChanged; clients
// extrapolate it from Rate and get a Seeked signal on jumps.
namespace jfd::platform {

  namespace {
    const QString ObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
    const QString RootInterface = QStringLiteral("org.mpris.MediaPlayer2");
    const QString PlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
    const QString TrackPath = QStringLiteral("/io/github/devln737/JellyfinDesktopNext/track");
    const QString NoTrackPath = QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");
    // A position this far from the extrapolated one counts as a seek.
    constexpr qint64 SeekThresholdMs = 1500;

    class MprisMediaControls;

    class RootAdaptor final : public QDBusAbstractAdaptor {
      Q_OBJECT
      Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
      Q_PROPERTY(bool CanQuit READ canQuit)
      Q_PROPERTY(bool CanRaise READ canRaise)
      Q_PROPERTY(bool HasTrackList READ hasTrackList)
      Q_PROPERTY(QString Identity READ identity)
      Q_PROPERTY(QString DesktopEntry READ desktopEntry)
      Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
      Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
    public:
      RootAdaptor(MprisMediaControls* controls, QObject* object);
      bool canQuit() const { return true; }
      bool canRaise() const { return true; }
      bool hasTrackList() const { return false; }
      QString identity() const;
      QString desktopEntry() const;
      QStringList supportedUriSchemes() const { return {}; }
      QStringList supportedMimeTypes() const { return {}; }

    public Q_SLOTS:
      void Raise();
      void Quit();

    private:
      MprisMediaControls* m_controls;
    };

    class PlayerAdaptor final : public QDBusAbstractAdaptor {
      Q_OBJECT
      Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
      Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
      Q_PROPERTY(double Rate READ rate WRITE setRate)
      Q_PROPERTY(double MinimumRate READ rate)
      Q_PROPERTY(double MaximumRate READ rate)
      Q_PROPERTY(QVariantMap Metadata READ metadata)
      Q_PROPERTY(double Volume READ volume WRITE setVolume)
      Q_PROPERTY(qlonglong Position READ position)
      Q_PROPERTY(bool CanGoNext READ canGoNext)
      Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
      Q_PROPERTY(bool CanPlay READ canPlay)
      Q_PROPERTY(bool CanPause READ canPause)
      Q_PROPERTY(bool CanSeek READ canSeek)
      Q_PROPERTY(bool CanControl READ canControl)
    public:
      PlayerAdaptor(MprisMediaControls* controls, QObject* object);
      QString playbackStatus() const;
      double rate() const;
      // Playback speed belongs to the session (and the group); not changed from here.
      void setRate(double) {}
      QVariantMap metadata() const;
      double volume() const { return 1.0; }
      void setVolume(double) {}
      qlonglong position() const;
      bool canGoNext() const;
      bool canGoPrevious() const;
      bool canPlay() const;
      bool canPause() const;
      bool canSeek() const;
      bool canControl() const { return true; }

    public Q_SLOTS:
      void Next();
      void Previous();
      void Pause();
      void PlayPause();
      void Stop();
      void Play();
      void Seek(qlonglong offsetUs);
      void SetPosition(const QDBusObjectPath& track, qlonglong positionUs);
      void OpenUri(const QString&) {}

    Q_SIGNALS:
      void Seeked(qlonglong positionUs);

    private:
      MprisMediaControls* m_controls;
    };

    class MprisMediaControls final : public MediaControls {
    public:
      MprisMediaControls(const QDBusConnection& bus, const QString& busSuffix, QString applicationName,
                         QString desktopEntry, QObject* parent)
          : MediaControls(parent), m_bus(bus), m_applicationName(std::move(applicationName)),
            m_desktopEntry(std::move(desktopEntry)) {
        // The adaptors live on a plain object so the exported interfaces are
        // exactly the two MPRIS ones.
        m_root = new RootAdaptor(this, &m_object);
        m_player = new PlayerAdaptor(this, &m_object);
        if (!m_bus.isConnected() || !m_bus.registerObject(ObjectPath, &m_object)) return;
        const QString base = QStringLiteral("org.mpris.MediaPlayer2.") + busSuffix;
        m_serviceName = base;
        if (!m_bus.registerService(m_serviceName)) {
          m_serviceName = base + QStringLiteral(".instance") +
                          QString::number(QCoreApplication::applicationPid());
          if (!m_bus.registerService(m_serviceName)) {
            m_bus.unregisterObject(ObjectPath);
            m_serviceName.clear();
            return;
          }
        }
        m_available = true;
      }

      ~MprisMediaControls() override {
        if (!m_available) return;
        m_bus.unregisterService(m_serviceName);
        m_bus.unregisterObject(ObjectPath);
      }

      bool isAvailable() const override { return m_available; }
      void attachWindow(QWindow*) override {}

      void setState(const MediaControlsState& state) override {
        const MediaControlsState previous = m_state;
        const qint64 expected = extrapolatedPositionMs();
        m_state = state;
        m_positionClock.start();
        if (!m_available) return;

        QVariantMap changed;
        auto note = [&](bool differs, const char* name, const QVariant& value) {
          if (differs) changed.insert(QString::fromLatin1(name), value);
        };
        note(previous.status != state.status, "PlaybackStatus", m_player->playbackStatus());
        note(previous.rate != state.rate, "Rate", state.rate);
        note(previous.title != state.title || previous.subtitle != state.subtitle ||
                 previous.artUrl != state.artUrl || previous.durationMs != state.durationMs,
             "Metadata", m_player->metadata());
        note(previous.canGoNext != state.canGoNext, "CanGoNext", state.canGoNext);
        note(previous.canGoPrevious != state.canGoPrevious, "CanGoPrevious", state.canGoPrevious);
        note(previous.canSeek != state.canSeek, "CanSeek", state.canSeek);
        note(previous.status != state.status, "CanPlay", m_player->canPlay());
        note(previous.status != state.status, "CanPause", m_player->canPause());
        if (!changed.isEmpty()) {
          QDBusMessage signal = QDBusMessage::createSignal(
              ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
          signal << PlayerInterface << changed << QStringList();
          m_bus.send(signal);
        }
        const bool newItem = previous.title != state.title || previous.status == MediaControlsState::Status::Stopped;
        if (state.status != MediaControlsState::Status::Stopped &&
            (newItem || std::abs(state.positionMs - expected) > SeekThresholdMs))
          Q_EMIT m_player->Seeked(state.positionMs * 1000);
      }

      const MediaControlsState& state() const { return m_state; }
      const QString& applicationName() const { return m_applicationName; }
      const QString& desktopEntry() const { return m_desktopEntry; }
      QString serviceName() const { return m_serviceName; }

      qint64 extrapolatedPositionMs() const {
        if (m_state.status != MediaControlsState::Status::Playing || !m_positionClock.isValid())
          return m_state.positionMs;
        const auto elapsed = static_cast<double>(m_positionClock.elapsed());
        return m_state.positionMs + static_cast<qint64>(elapsed * m_state.rate);
      }

      void emitCommand(MediaCommand command) { Q_EMIT commandRequested(command); }
      void emitSeek(qint64 positionMs) {
        const qint64 duration = m_state.durationMs > 0 ? m_state.durationMs : positionMs;
        Q_EMIT seekRequested(std::clamp<qint64>(positionMs, 0, duration));
      }
      void emitRaise() { Q_EMIT raiseRequested(); }
      void emitQuit() { Q_EMIT quitRequested(); }

    private:
      QDBusConnection m_bus;
      QObject m_object;
      RootAdaptor* m_root = nullptr;
      PlayerAdaptor* m_player = nullptr;
      QString m_applicationName;
      QString m_desktopEntry;
      QString m_serviceName;
      MediaControlsState m_state;
      QElapsedTimer m_positionClock;
      bool m_available = false;
    };

    RootAdaptor::RootAdaptor(MprisMediaControls* controls, QObject* object)
        : QDBusAbstractAdaptor(object), m_controls(controls) {}
    QString RootAdaptor::identity() const { return m_controls->applicationName(); }
    QString RootAdaptor::desktopEntry() const { return m_controls->desktopEntry(); }
    void RootAdaptor::Raise() { m_controls->emitRaise(); }
    void RootAdaptor::Quit() { m_controls->emitQuit(); }

    PlayerAdaptor::PlayerAdaptor(MprisMediaControls* controls, QObject* object)
        : QDBusAbstractAdaptor(object), m_controls(controls) {}

    QString PlayerAdaptor::playbackStatus() const {
      switch (m_controls->state().status) {
      case MediaControlsState::Status::Playing: return QStringLiteral("Playing");
      case MediaControlsState::Status::Paused: return QStringLiteral("Paused");
      case MediaControlsState::Status::Stopped: break;
      }
      return QStringLiteral("Stopped");
    }

    double PlayerAdaptor::rate() const { return m_controls->state().rate; }

    QVariantMap PlayerAdaptor::metadata() const {
      const MediaControlsState& state = m_controls->state();
      if (state.status == MediaControlsState::Status::Stopped && state.title.isEmpty())
        return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(NoTrackPath))}};
      QVariantMap metadata{{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(TrackPath))},
                           {QStringLiteral("xesam:title"), state.title}};
      if (!state.subtitle.isEmpty()) {
        metadata.insert(QStringLiteral("xesam:album"), state.subtitle);
        metadata.insert(QStringLiteral("xesam:artist"), QStringList{state.subtitle});
      }
      if (state.durationMs > 0) metadata.insert(QStringLiteral("mpris:length"), qlonglong(state.durationMs) * 1000);
      if (state.artUrl.isValid()) metadata.insert(QStringLiteral("mpris:artUrl"), state.artUrl.toString());
      return metadata;
    }

    qlonglong PlayerAdaptor::position() const { return m_controls->extrapolatedPositionMs() * 1000; }
    bool PlayerAdaptor::canGoNext() const { return m_controls->state().canGoNext; }
    bool PlayerAdaptor::canGoPrevious() const { return m_controls->state().canGoPrevious; }
    bool PlayerAdaptor::canPlay() const { return m_controls->state().status != MediaControlsState::Status::Stopped; }
    bool PlayerAdaptor::canPause() const { return canPlay(); }
    bool PlayerAdaptor::canSeek() const { return m_controls->state().canSeek; }

    void PlayerAdaptor::Next() {
      if (canGoNext()) m_controls->emitCommand(MediaCommand::Next);
    }
    void PlayerAdaptor::Previous() {
      if (canGoPrevious()) m_controls->emitCommand(MediaCommand::Previous);
    }
    void PlayerAdaptor::Pause() {
      if (canPause()) m_controls->emitCommand(MediaCommand::Pause);
    }
    void PlayerAdaptor::PlayPause() {
      if (canPlay()) m_controls->emitCommand(MediaCommand::PlayPause);
    }
    void PlayerAdaptor::Stop() { m_controls->emitCommand(MediaCommand::Stop); }
    void PlayerAdaptor::Play() {
      if (canPlay()) m_controls->emitCommand(MediaCommand::Play);
    }
    void PlayerAdaptor::Seek(qlonglong offsetUs) {
      if (canSeek()) m_controls->emitSeek(m_controls->extrapolatedPositionMs() + offsetUs / 1000);
    }
    void PlayerAdaptor::SetPosition(const QDBusObjectPath& track, qlonglong positionUs) {
      // The spec ignores requests for another track or outside the item.
      if (!canSeek() || track.path() != TrackPath || positionUs < 0) return;
      if (m_controls->state().durationMs > 0 && positionUs / 1000 > m_controls->state().durationMs) return;
      m_controls->emitSeek(positionUs / 1000);
    }
  } // namespace

  std::unique_ptr<MediaControls> createMprisMediaControls(const QDBusConnection& bus, const QString& busSuffix,
                                                          const QString& applicationName,
                                                          const QString& desktopEntry, QObject* parent) {
    return std::make_unique<MprisMediaControls>(bus, busSuffix, applicationName, desktopEntry, parent);
  }

  std::unique_ptr<MediaControls> createSystemMediaControls(const QString& applicationName,
                                                           const QString& desktopEntry, QObject* parent) {
    // Bus names allow letters, digits and underscores in each element.
    QString suffix = applicationName.toLower();
    for (QChar& c : suffix)
      if (!c.isLetterOrNumber() || c.unicode() > 127) c = QLatin1Char('_');
    return createMprisMediaControls(QDBusConnection::sessionBus(), suffix, applicationName, desktopEntry,
                                    parent);
  }

} // namespace jfd::platform

#include "MediaControlsMpris.moc"
