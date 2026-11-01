#pragma once

#include <jfd/api/Models.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::ui {

  // The header of an album, artist or playlist page: what the server says
  // about the item itself.
  class MusicItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString itemId READ itemId WRITE setItemId NOTIFY itemChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY itemChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY itemChanged)
    Q_PROPERTY(QString name READ name NOTIFY itemChanged)
    Q_PROPERTY(QString type READ type NOTIFY itemChanged)
    // The album's artist; empty for artists and playlists.
    Q_PROPERTY(QString artistName READ artistName NOTIFY itemChanged)
    Q_PROPERTY(QString artistId READ artistId NOTIFY itemChanged)
    Q_PROPERTY(QVariant year READ year NOTIFY itemChanged)
    Q_PROPERTY(QString overview READ overview NOTIFY itemChanged)
    Q_PROPERTY(QStringList genres READ genres NOTIFY itemChanged)
    Q_PROPERTY(QUrl imageUrl READ imageUrl NOTIFY itemChanged)
    Q_PROPERTY(bool favorite READ favorite NOTIFY itemChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY itemChanged)

  public:
    MusicItem(api::JellyfinClient& client, int imageWidth, qreal devicePixelRatio, QObject* parent = nullptr);

    QString itemId() const { return m_itemId; }
    void setItemId(const QString& id);
    bool loaded() const { return m_item.has_value(); }
    QString errorText() const { return m_errorText; }
    QString name() const { return m_item ? m_item->name : QString(); }
    QString type() const { return m_item ? m_item->type : QString(); }
    QString artistName() const;
    QString artistId() const;
    QVariant year() const;
    QString overview() const { return m_item ? m_item->overview : QString(); }
    QStringList genres() const { return m_item ? m_item->genres : QStringList(); }
    QUrl imageUrl() const { return m_imageUrl; }
    bool favorite() const { return m_item && m_item->userData.isFavorite; }
    int trackCount() const;

    Q_INVOKABLE void toggleFavorite();

  Q_SIGNALS:
    void itemChanged();

  private:
    api::JellyfinClient& m_client;
    int m_imageWidth;
    qreal m_devicePixelRatio;
    QString m_itemId;
    std::optional<api::Item> m_item;
    QUrl m_imageUrl;
    QString m_errorText;
    quint64 m_serial = 0;
  };

} // namespace jfd::ui
