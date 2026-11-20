#include <jfd/offline/Manifest.h>
#include <jfd/offline/DownloadStore.h>

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace jfd::offline {

  namespace {
    const QLatin1String StateNames[] = {QLatin1String("queued"), QLatin1String("paused"),
                                        QLatin1String("failed"), QLatin1String("complete")};

    bool isSecretKey(const QString& key) {
      return key.compare(QLatin1String("TranscodingUrl"), Qt::CaseInsensitive) == 0 ||
             key.compare(QLatin1String("DirectStreamUrl"), Qt::CaseInsensitive) == 0 ||
             key.compare(QLatin1String("ApiKey"), Qt::CaseInsensitive) == 0 ||
             key.compare(QLatin1String("api_key"), Qt::CaseInsensitive) == 0 ||
             key.compare(QLatin1String("AccessToken"), Qt::CaseInsensitive) == 0;
    }

    // Cuts "ApiKey=...", "api_key=...", "X-Emby-Token=..." and the like out
    // of a string, with the separator before them.
    QString scrubString(const QString& value) {
      static const QRegularExpression parameter(
          QStringLiteral(
              "([?&;])(api_?key|x-emby-token|x-mediabrowser-token|access_?token)=[^&;#\\s\"]*"),
          QRegularExpression::CaseInsensitiveOption);
      if (!value.contains(QLatin1Char('=')) || !parameter.match(value).hasMatch()) return value;
      QString out = value;
      out.replace(parameter, QStringLiteral("\\1"));
      // "?&a=1" -> "?a=1", trailing "?" or "&" -> nothing.
      static const QRegularExpression doubled(QStringLiteral("([?&;])[&;]+"));
      out.replace(doubled, QStringLiteral("\\1"));
      static const QRegularExpression trailing(QStringLiteral("[?&;]+(?=$|#)"));
      out.replace(trailing, QString());
      return out;
    }

    QJsonValue scrubValue(const QJsonValue& value);

    QJsonArray scrubArray(const QJsonArray& array) {
      QJsonArray out;
      for (const auto& v : array)
        out.append(scrubValue(v));
      return out;
    }

    QJsonValue scrubValue(const QJsonValue& value) {
      if (value.isObject()) return scrubSecrets(value.toObject());
      if (value.isArray()) return scrubArray(value.toArray());
      if (value.isString()) return scrubString(value.toString());
      return value;
    }

    QString date(const QDateTime& when) {
      return when.isValid() ? when.toUTC().toString(Qt::ISODateWithMs) : QString();
    }
  } // namespace

  QString stateName(ItemState state) {
    return StateNames[static_cast<int>(state)];
  }

  std::optional<ItemState> stateFromName(const QString& name) {
    for (int i = 0; i < 4; ++i)
      if (name == StateNames[i]) return static_cast<ItemState>(i);
    return std::nullopt;
  }

  QJsonObject scrubSecrets(const QJsonObject& object) {
    QJsonObject out;
    for (auto it = object.begin(); it != object.end(); ++it) {
      if (isSecretKey(it.key())) continue;
      out.insert(it.key(), scrubValue(it.value()));
    }
    return out;
  }

  QJsonObject toJson(const ItemManifest& m) {
    QJsonObject o;
    o.insert(QStringLiteral("format"), m.format == 1 ? 1 : ItemManifest::FormatVersion);
    o.insert(QStringLiteral("serverId"), m.serverId);
    o.insert(QStringLiteral("userId"), m.userId);
    o.insert(QStringLiteral("itemId"), m.itemId);
    o.insert(QStringLiteral("state"), stateName(m.state));
    o.insert(QStringLiteral("quality"), m.quality);
    if (m.quality == QLatin1String("compressed")) {
      const TranscodeState& t = m.transcode;
      QJsonObject transcode{{QStringLiteral("preset"), t.preset},
                            {QStringLiteral("audioStreamIndex"), t.audioStreamIndex}};
      if (!t.videoCodec.isEmpty()) transcode.insert(QStringLiteral("videoCodec"), t.videoCodec);
      if (!t.audioCodec.isEmpty()) transcode.insert(QStringLiteral("audioCodec"), t.audioCodec);
      if (t.segmentCount > 0) transcode.insert(QStringLiteral("segmentCount"), t.segmentCount);
      if (!t.signature.isEmpty()) transcode.insert(QStringLiteral("signature"), t.signature);
      if (!t.initHash.isEmpty()) transcode.insert(QStringLiteral("initHash"), t.initHash);
      if (t.segmentsDone > 0) transcode.insert(QStringLiteral("segmentsDone"), t.segmentsDone);
      if (t.partBytes > 0)
        transcode.insert(QStringLiteral("partBytes"), static_cast<double>(t.partBytes));
      o.insert(QStringLiteral("transcode"), scrubSecrets(transcode));
    }
    o.insert(QStringLiteral("mediaFile"), m.mediaFile);
    if (m.mediaComplete) o.insert(QStringLiteral("mediaComplete"), true);
    // Whole bytes fit a double exactly up to 2^53 (8 PiB).
    o.insert(QStringLiteral("expectedSize"), static_cast<double>(m.expectedSize));
    if (!m.resumeValidator.isEmpty())
      o.insert(QStringLiteral("resumeValidator"), m.resumeValidator);
    o.insert(QStringLiteral("queuedAt"), date(m.queuedAt));
    if (m.completedAt.isValid()) o.insert(QStringLiteral("completedAt"), date(m.completedAt));
    o.insert(QStringLiteral("item"), scrubSecrets(m.item));
    o.insert(QStringLiteral("mediaSource"), scrubSecrets(m.mediaSource));
    o.insert(QStringLiteral("userData"), scrubSecrets(m.userData));
    if (!m.subtitleFiles.isEmpty()) {
      QJsonObject subtitles;
      for (auto it = m.subtitleFiles.begin(); it != m.subtitleFiles.end(); ++it)
        subtitles.insert(QString::number(it.key()), it.value());
      o.insert(QStringLiteral("subtitleFiles"), subtitles);
    }
    if (!m.audioFiles.isEmpty()) {
      QJsonObject audio;
      for (auto it = m.audioFiles.begin(); it != m.audioFiles.end(); ++it)
        audio.insert(QString::number(it.key()), it.value());
      o.insert(QStringLiteral("audioFiles"), audio);
    }
    if (!m.audioSkipped.isEmpty()) {
      QJsonArray skipped;
      for (const int index : m.audioSkipped) skipped.append(index);
      o.insert(QStringLiteral("audioSkipped"), skipped);
    }
    if (!m.imageFiles.isEmpty()) {
      QJsonObject images;
      for (auto it = m.imageFiles.begin(); it != m.imageFiles.end(); ++it)
        images.insert(it.key(), it.value());
      o.insert(QStringLiteral("imageFiles"), images);
    }
    if (!m.lyricsFile.isEmpty()) o.insert(QStringLiteral("lyricsFile"), m.lyricsFile);
    if (!m.segmentsFile.isEmpty()) o.insert(QStringLiteral("segmentsFile"), m.segmentsFile);
    if (m.extrasComplete) o.insert(QStringLiteral("extrasComplete"), true);
    if (!m.parents.isEmpty())
      o.insert(QStringLiteral("parents"), QJsonArray::fromStringList(m.parents));
    if (m.removedOnServer) o.insert(QStringLiteral("removedOnServer"), true);
    if (m.changedOnServer) o.insert(QStringLiteral("changedOnServer"), true);
    if (m.refreshedAt.isValid()) o.insert(QStringLiteral("refreshedAt"), date(m.refreshedAt));
    return o;
  }

  std::optional<ItemManifest> manifestFromJson(const QJsonObject& o) {
    const int format = o.value(QLatin1String("format")).toInt();
    if (format != 1 && format != ItemManifest::FormatVersion) return std::nullopt;
    ItemManifest m;
    m.format = format;
    m.serverId = o.value(QLatin1String("serverId")).toString();
    m.userId = o.value(QLatin1String("userId")).toString();
    m.itemId = o.value(QLatin1String("itemId")).toString();
    if (m.serverId.isEmpty() || m.userId.isEmpty() || m.itemId.isEmpty()) return std::nullopt;
    const auto state = stateFromName(o.value(QLatin1String("state")).toString());
    if (!state) return std::nullopt;
    m.state = *state;
    m.quality = o.value(QLatin1String("quality")).toString(QStringLiteral("original"));
    if (m.quality == QLatin1String("compressed")) {
      const QJsonObject t = o.value(QLatin1String("transcode")).toObject();
      m.transcode.preset = t.value(QLatin1String("preset")).toString();
      m.transcode.audioStreamIndex = t.value(QLatin1String("audioStreamIndex")).toInt(-1);
      m.transcode.videoCodec = t.value(QLatin1String("videoCodec")).toString();
      m.transcode.audioCodec = t.value(QLatin1String("audioCodec")).toString();
      m.transcode.segmentCount = std::max(0, t.value(QLatin1String("segmentCount")).toInt());
      m.transcode.signature = t.value(QLatin1String("signature")).toString();
      m.transcode.initHash = t.value(QLatin1String("initHash")).toString();
      m.transcode.segmentsDone = std::clamp(t.value(QLatin1String("segmentsDone")).toInt(), 0,
                                            m.transcode.segmentCount);
      const double bytes = t.value(QLatin1String("partBytes")).toDouble();
      m.transcode.partBytes =
          bytes > 0 && bytes < 9007199254740992.0 ? static_cast<std::int64_t>(bytes) : 0;
    }
    m.mediaFile = o.value(QLatin1String("mediaFile")).toString();
    m.mediaComplete = format != 1 && o.value(QLatin1String("mediaComplete")).toBool();
    const double size = o.value(QLatin1String("expectedSize")).toDouble();
    m.expectedSize = size > 0 && size < 9007199254740992.0 ? static_cast<std::int64_t>(size) : 0;
    m.resumeValidator = o.value(QLatin1String("resumeValidator")).toString();
    m.queuedAt =
        QDateTime::fromString(o.value(QLatin1String("queuedAt")).toString(), Qt::ISODateWithMs);
    m.completedAt =
        QDateTime::fromString(o.value(QLatin1String("completedAt")).toString(), Qt::ISODateWithMs);
    m.item = o.value(QLatin1String("item")).toObject();
    m.mediaSource = o.value(QLatin1String("mediaSource")).toObject();
    m.userData = o.value(QLatin1String("userData")).toObject();
    // Files of the item are relative paths inside its folder (or, format 2,
    // the download folder): nothing that climbs out.
    const auto inside = [](const QString& file) {
      if (file.isEmpty() || file.contains(QLatin1Char('\\')) || file.contains(QLatin1Char(':')))
        return false;
      for (const QString& part : file.split(QLatin1Char('/')))
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) return false;
      return true;
    };
    if (!m.mediaFile.isEmpty() && !inside(m.mediaFile)) m.mediaFile.clear();
    const QJsonObject subtitles = o.value(QLatin1String("subtitleFiles")).toObject();
    for (auto it = subtitles.begin(); it != subtitles.end(); ++it) {
      bool ok = false;
      const int index = it.key().toInt(&ok);
      if (ok && inside(it.value().toString())) m.subtitleFiles.insert(index, it.value().toString());
    }
    if (format != 1) {
      const QJsonObject audio = o.value(QLatin1String("audioFiles")).toObject();
      for (auto it = audio.begin(); it != audio.end(); ++it) {
        bool ok = false;
        const int index = it.key().toInt(&ok);
        if (ok && inside(it.value().toString())) m.audioFiles.insert(index, it.value().toString());
      }
      for (const QJsonValue& index : o.value(QLatin1String("audioSkipped")).toArray())
        if (index.isDouble() && !m.audioSkipped.contains(index.toInt())) m.audioSkipped.append(index.toInt());
    }
    const QJsonObject images = o.value(QLatin1String("imageFiles")).toObject();
    for (auto it = images.begin(); it != images.end(); ++it)
      if (inside(it.value().toString())) m.imageFiles.insert(it.key(), it.value().toString());
    if (const QString f = o.value(QLatin1String("lyricsFile")).toString(); inside(f)) m.lyricsFile = f;
    if (const QString f = o.value(QLatin1String("segmentsFile")).toString(); inside(f))
      m.segmentsFile = f;
    m.extrasComplete = o.value(QLatin1String("extrasComplete")).toBool();
    for (const QJsonValue& parent : o.value(QLatin1String("parents")).toArray())
      if (parent.isString() && DownloadStore::isSafeId(parent.toString()))
        m.parents.append(parent.toString());
    m.removedOnServer = o.value(QLatin1String("removedOnServer")).toBool();
    m.changedOnServer = o.value(QLatin1String("changedOnServer")).toBool();
    m.refreshedAt =
        QDateTime::fromString(o.value(QLatin1String("refreshedAt")).toString(), Qt::ISODateWithMs);
    return m;
  }

  QString mediaExtension(const QJsonObject& mediaSource) {
    static const QRegularExpression safe(QStringLiteral("^[a-z0-9]{1,8}$"));
    const QString container = mediaSource.value(QLatin1String("Container"))
                                  .toString()
                                  .section(QLatin1Char(','), 0, 0)
                                  .trimmed()
                                  .toLower();
    if (safe.match(container).hasMatch()) return container;
    const QString path = mediaSource.value(QLatin1String("Path")).toString();
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0) {
      const QString extension = path.mid(dot + 1).toLower();
      if (safe.match(extension).hasMatch()) return extension;
    }
    return QStringLiteral("bin");
  }

} // namespace jfd::offline
