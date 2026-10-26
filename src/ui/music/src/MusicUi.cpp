#include <jfd/ui/MusicUi.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/CardImages.h>
#include <jfd/music/MusicPlayer.h>

#include <QCoreApplication>

namespace jfd::ui {

  MusicUi::MusicUi(Config config, QObject* parent) : QObject(parent), m_config(config) {
    if (m_config.client) {
      connect(m_config.client, &api::JellyfinClient::sessionChanged, this, &MusicUi::refresh);
      refresh();
    }
  }

  void MusicUi::refresh() {
    if (!m_config.client || !m_config.client->isSignedIn()) {
      if (m_available) {
        m_available = false;
        m_libraryId.clear();
        Q_EMIT availableChanged();
      }
      return;
    }
    const quint64 serial = ++m_serial;
    m_config.client->views(this, [this, serial](api::Result<api::ItemPage> result) {
      if (serial != m_serial || !result) return;
      QStringList music;
      for (const api::Item& view : result->items)
        if (view.collectionType == QLatin1String("music")) music.append(view.id);
      const bool available = !music.isEmpty();
      const QString id = music.size() == 1 ? music.front() : QString();
      if (available == m_available && id == m_libraryId) return;
      m_available = available;
      m_libraryId = id;
      Q_EMIT availableChanged();
    });
  }

  MusicListModel* MusicUi::createList(const QString& kind, QObject* owner) {
    MusicListModel::Options options;
    options.devicePixelRatio = m_config.devicePixelRatio;
    auto* model = new MusicListModel(*m_config.client, *m_config.player, options, owner);
    model->setKind(kind);
    // Lists of the whole library; an album's or playlist's tracks set
    // their own parent.
    if (kind != QLatin1String("albumTracks") && kind != QLatin1String("playlistTracks"))
      model->setParentId(m_libraryId);
    return model;
  }

  MusicItem* MusicUi::createItem(const QString& itemId, QObject* owner) {
    auto* item = new MusicItem(*m_config.client, 300, m_config.devicePixelRatio, owner);
    item->setItemId(itemId);
    return item;
  }

  QUrl MusicUi::coverUrl(const QString& itemId, const QString& tag, int width) const {
    if (!m_config.client || itemId.isEmpty() || tag.isEmpty()) return {};
    const QSize pixels = library::cardImageSize(library::CardShape::Square, width, m_config.devicePixelRatio);
    api::ImageRequest request;
    request.type = QStringLiteral("Primary");
    request.fillWidth = pixels.width();
    request.fillHeight = pixels.height();
    request.itemId = itemId;
    request.tag = tag;
    return m_config.client->imageUrl(request);
  }

  QString MusicUi::formatDuration(double seconds) const {
    const qint64 total = static_cast<qint64>(seconds < 0 ? 0 : seconds);
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 secs = total % 60;
    if (hours > 0)
      return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
  }

  QString MusicUi::formatTotal(double seconds) const {
    const qint64 minutes = static_cast<qint64>(seconds / 60.0 + 0.5);
    if (minutes < 60) return QCoreApplication::translate("MusicList", "%1 min").arg(minutes);
    return QCoreApplication::translate("MusicList", "%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
  }

  music::MusicPlayer* MusicSession::create(QQmlEngine*, QJSEngine* engine) {
    Q_ASSERT(s_instance != nullptr);
    Q_ASSERT(engine->thread() == s_instance->thread());
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
  }

} // namespace jfd::ui
