// The key screens on a demo catalogue served by an in-process server, for
// screenshots of the look (home with its banner, a series, a film, a
// library) and checks that the new parts show up: the banner with its
// items, the logo in place of the title, the search field over it.
//
// The pictures are drawn here (gradients, shapes and the title), so the
// screens look like a real library without any third-party artwork.
//
// JFD_UI_SCREENSHOTS=<dir> saves a screenshot of each screen at 1280x800,
// 1600x900 (the mockups' size) and 1920x1080 (and 860x700 for the narrow
// layout).

#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/HomeModel.h>
#include <jfd/platform/CredentialStore.h>
#include <jfd/ui/LibraryScreens.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/settings/CommonSections.h>
#include <jfd/settings/Settings.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/SettingsModel.h>
#include <jfd/ui/ThemePalette.h>

#include <QBuffer>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTest>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtQml/qqmlextensionplugin.h>

#include <cmath>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_SettingsPlugin)

using namespace jfd;
using jfd::test::waitFor;

namespace {

  constexpr int Timeout = 20000;
  constexpr qint64 TicksPerMinute = 600'000'000;

  // --- The catalogue ----------------------------------------------------------

  struct Title {
    QString id;
    QString name;
    QString type; // "Movie" or "Series"
    QString library;
    int year;
    QStringList genres;
    QString rating;
    double stars;
    int minutes;
    int hue;
    bool logo;
    QString overview;
  };

  const QList<Title>& titles() {
    static const QList<Title> list{
        {QStringLiteral("s1"), QStringLiteral("Хроники маяка"), QStringLiteral("Series"), QStringLiteral("tv"), 2023,
         {QStringLiteral("Драма"), QStringLiteral("Мистика")}, QStringLiteral("16+"), 7.9, 48, 200, true,
         QStringLiteral("Смотритель маяка на северном острове находит журнал, который пишет сам себя. Каждая запись "
                        "предсказывает шторм, и каждый шторм приносит на берег кого-то из прошлого.")},
        {QStringLiteral("s2"), QStringLiteral("Сигнал из глубины"), QStringLiteral("Series"), QStringLiteral("tv"), 2021,
         {QStringLiteral("Фантастика")}, QStringLiteral("12+"), 7.2, 42, 170, false,
         QStringLiteral("Экипаж подводной станции принимает сигнал, которого не может быть.")},
        {QStringLiteral("s3"), QStringLiteral("Город после дождя"), QStringLiteral("Series"), QStringLiteral("tv"), 2024,
         {QStringLiteral("Детектив")}, QStringLiteral("18+"), 8.1, 55, 260, true,
         QStringLiteral("Следователь возвращается в родной город, где за одну ночь исчезли все часы.")},
        {QStringLiteral("m1"), QStringLiteral("Северный ветер"), QStringLiteral("Movie"), QStringLiteral("movies"), 2024,
         {QStringLiteral("Приключения"), QStringLiteral("Драма")}, QStringLiteral("12+"), 7.6, 124, 190, true,
         QStringLiteral("Два брата идут через тундру, чтобы вернуть отцовский компас, и узнают, куда он на самом деле "
                        "показывал все эти годы.")},
        {QStringLiteral("m2"), QStringLiteral("Стеклянный сад"), QStringLiteral("Movie"), QStringLiteral("movies"), 2022,
         {QStringLiteral("Мелодрама")}, QStringLiteral("16+"), 6.9, 108, 320, false,
         QStringLiteral("Оранжерея на крыше старого дома хранит письма тех, кто когда-то здесь жил.")},
        {QStringLiteral("m3"), QStringLiteral("Последний рейс"), QStringLiteral("Movie"), QStringLiteral("movies"), 2019,
         {QStringLiteral("Триллер")}, QStringLiteral("18+"), 7.0, 97, 20, true,
         QStringLiteral("Ночной поезд, шесть пассажиров и одна станция, которой нет на карте.")},
        {QStringLiteral("m4"), QStringLiteral("Тихая гавань"), QStringLiteral("Movie"), QStringLiteral("movies"), 2023,
         {QStringLiteral("Комедия")}, QStringLiteral("6+"), 6.5, 92, 140, false,
         QStringLiteral("Рыбацкий посёлок готовится к приезду проверки и придумывает себе достопримечательность.")},
        {QStringLiteral("m5"), QStringLiteral("Ночной экспресс"), QStringLiteral("Movie"), QStringLiteral("movies"), 2020,
         {QStringLiteral("Боевик")}, QStringLiteral("16+"), 6.8, 115, 230, false,
         QStringLiteral("Курьер должен доставить посылку до рассвета, не открывая её.")},
        {QStringLiteral("m6"), QStringLiteral("Карта звёзд"), QStringLiteral("Movie"), QStringLiteral("movies"), 2021,
         {QStringLiteral("Фантастика")}, QStringLiteral("12+"), 7.4, 131, 250, true,
         QStringLiteral("Астроном-любитель находит на старой карте звезду, которая появилась только вчера.")},
        {QStringLiteral("m7"), QStringLiteral("Медный лес"), QStringLiteral("Movie"), QStringLiteral("movies"), 2018,
         {QStringLiteral("Фэнтези")}, QStringLiteral("12+"), 7.1, 101, 35, false,
         QStringLiteral("Осенью деревья в лесу звенят, и каждый звон кому-то что-то обещает.")},
    };
    return list;
  }

  const Title* titleById(const QString& id) {
    for (const Title& t : titles())
      if (t.id == id) return &t;
    return nullptr;
  }

  const QStringList& episodeNames() {
    static const QStringList names{QStringLiteral("Журнал"),        QStringLiteral("Первый шторм"),
                                   QStringLiteral("Новый жилец"),    QStringLiteral("Огни на воде"),
                                   QStringLiteral("Гость с берега"), QStringLiteral("Полный штиль"),
                                   QStringLiteral("Туман"),          QStringLiteral("Последняя запись")};
    return names;
  }

  // Series s1 has two seasons of 8; the others one season of 6.
  int seasonsOf(const QString& seriesId) { return seriesId == QLatin1String("s1") ? 2 : 1; }
  int episodesIn(const QString& seriesId) { return seriesId == QLatin1String("s1") ? 8 : 6; }

  // "Continue watching": s1 S1E3 at 12:40 of 48 min; m1 at 40 min.
  constexpr qint64 ResumeEpisodeTicks = 12 * TicksPerMinute + 40 * 10'000'000LL;
  constexpr qint64 ResumeMovieTicks = 40 * TicksPerMinute;

  QJsonObject userData(bool played, qint64 position, bool favorite, std::optional<int> unplayed = {}) {
    QJsonObject data{{QStringLiteral("Played"), played},
                     {QStringLiteral("PlaybackPositionTicks"), position},
                     {QStringLiteral("IsFavorite"), favorite}};
    if (unplayed) data.insert(QStringLiteral("UnplayedItemCount"), *unplayed);
    return data;
  }

  QJsonObject titleJson(const Title& t) {
    QJsonObject tags{{QStringLiteral("Primary"), QStringLiteral("p")}};
    if (t.logo) tags.insert(QStringLiteral("Logo"), QStringLiteral("l"));
    const bool series = t.type == QLatin1String("Series");
    QJsonObject o{{QStringLiteral("Id"), t.id},
                  {QStringLiteral("Name"), t.name},
                  {QStringLiteral("Type"), t.type},
                  {QStringLiteral("IsFolder"), series},
                  {QStringLiteral("ProductionYear"), t.year},
                  {QStringLiteral("Genres"), QJsonArray::fromStringList(t.genres)},
                  {QStringLiteral("OfficialRating"), t.rating},
                  {QStringLiteral("CommunityRating"), t.stars},
                  {QStringLiteral("Overview"), t.overview},
                  {QStringLiteral("ImageTags"), tags},
                  {QStringLiteral("BackdropImageTags"), QJsonArray{QStringLiteral("b")}},
                  {QStringLiteral("PrimaryImageAspectRatio"), 0.6667}};
    // People, studio and streams, as a detail page asks for them.
    const QStringList cast{QStringLiteral("Анна Северова"), QStringLiteral("Илья Громов"),
                           QStringLiteral("Мария Лесная"), QStringLiteral("Олег Тихонов"),
                           QStringLiteral("Вера Ключ"), QStringLiteral("Пётр Ветров")};
    const QStringList roles{QStringLiteral("Смотритель"), QStringLiteral("Капитан"), QStringLiteral("Врач"),
                            QStringLiteral("Рыбак"), QStringLiteral("Почтальон"), QStringLiteral("Гость")};
    QJsonArray people;
    for (int i = 0; i < cast.size(); ++i)
      people.append(QJsonObject{{QStringLiteral("Id"), QStringLiteral("p%1").arg(i)},
                                {QStringLiteral("Name"), cast[i]},
                                {QStringLiteral("Role"), roles[i]},
                                {QStringLiteral("Type"), QStringLiteral("Actor")},
                                {QStringLiteral("PrimaryImageTag"), QStringLiteral("p")}});
    people.append(QJsonObject{{QStringLiteral("Id"), QStringLiteral("p9")},
                              {QStringLiteral("Name"), QStringLiteral("Дарья Маяк")},
                              {QStringLiteral("Type"), QStringLiteral("Director")}});
    o.insert(QStringLiteral("People"), people);
    o.insert(QStringLiteral("Studios"), QJsonArray{QJsonObject{{QStringLiteral("Name"), QStringLiteral("Северная студия")}}});
    if (!series)
      o.insert(QStringLiteral("MediaStreams"),
               QJsonArray{QJsonObject{{QStringLiteral("Type"), QStringLiteral("Video")}, {QStringLiteral("Codec"), QStringLiteral("hevc")},
                                      {QStringLiteral("Width"), 1920}, {QStringLiteral("Height"), 804}},
                          QJsonObject{{QStringLiteral("Type"), QStringLiteral("Audio")}, {QStringLiteral("Language"), QStringLiteral("rus")},
                                      {QStringLiteral("Channels"), 6}},
                          QJsonObject{{QStringLiteral("Type"), QStringLiteral("Audio")}, {QStringLiteral("Language"), QStringLiteral("eng")},
                                      {QStringLiteral("Channels"), 2}},
                          QJsonObject{{QStringLiteral("Type"), QStringLiteral("Subtitle")}, {QStringLiteral("Language"), QStringLiteral("rus")}},
                          QJsonObject{{QStringLiteral("Type"), QStringLiteral("Subtitle")}, {QStringLiteral("Language"), QStringLiteral("eng")}}});
    else
      o.insert(QStringLiteral("RecursiveItemCount"), seasonsOf(t.id) * episodesIn(t.id));
    if (!series) {
      o.insert(QStringLiteral("RunTimeTicks"), t.minutes * TicksPerMinute);
      o.insert(QStringLiteral("MediaType"), QStringLiteral("Video"));
    } else {
      o.insert(QStringLiteral("ChildCount"), seasonsOf(t.id));
      o.insert(QStringLiteral("Status"), QStringLiteral("Continuing"));
    }
    const qint64 position = t.id == QLatin1String("m1") ? ResumeMovieTicks : 0;
    o.insert(QStringLiteral("UserData"),
             userData(t.id == QLatin1String("m4"), position, t.id == QLatin1String("s3"),
                      series ? std::optional<int>(t.id == QLatin1String("s1") ? 14 : episodesIn(t.id)) : std::nullopt));
    return o;
  }

  QString episodeId(const QString& seriesId, int season, int episode) {
    return QStringLiteral("%1e%2x%3").arg(seriesId).arg(season).arg(episode);
  }

  QJsonObject episodeJson(const Title& series, int season, int episode) {
    const QString id = episodeId(series.id, season, episode);
    const bool s1 = series.id == QLatin1String("s1");
    // s1: season 1 episodes 1-2 watched, 3 in progress.
    const bool played = s1 && season == 1 && episode < 3;
    const qint64 position = s1 && season == 1 && episode == 3 ? ResumeEpisodeTicks : 0;
    QJsonObject o{{QStringLiteral("Id"), id},
                  {QStringLiteral("Name"), episodeNames().value((episode - 1 + (season - 1) * 3) % episodeNames().size())},
                  {QStringLiteral("Type"), QStringLiteral("Episode")},
                  {QStringLiteral("MediaType"), QStringLiteral("Video")},
                  {QStringLiteral("SeriesId"), series.id},
                  {QStringLiteral("SeriesName"), series.name},
                  {QStringLiteral("SeasonId"), QStringLiteral("%1s%2").arg(series.id).arg(season)},
                  {QStringLiteral("SeasonName"), QStringLiteral("Сезон %1").arg(season)},
                  {QStringLiteral("IndexNumber"), episode},
                  {QStringLiteral("ParentIndexNumber"), season},
                  {QStringLiteral("ProductionYear"), series.year},
                  {QStringLiteral("RunTimeTicks"), series.minutes * TicksPerMinute},
                  {QStringLiteral("Overview"),
                   QStringLiteral("Шторм приходит раньше, чем обещал журнал, и на берегу появляются следы.")},
                  {QStringLiteral("ImageTags"), QJsonObject{{QStringLiteral("Primary"), QStringLiteral("p")}}},
                  {QStringLiteral("ParentBackdropItemId"), series.id},
                  {QStringLiteral("ParentBackdropImageTags"), QJsonArray{QStringLiteral("b")}},
                  {QStringLiteral("UserData"), userData(played, position, false)}};
    if (series.logo) {
      o.insert(QStringLiteral("ParentLogoItemId"), series.id);
      o.insert(QStringLiteral("ParentLogoImageTag"), QStringLiteral("l"));
    }
    return o;
  }

  QJsonObject seasonJson(const Title& series, int season) {
    return {{QStringLiteral("Id"), QStringLiteral("%1s%2").arg(series.id).arg(season)},
            {QStringLiteral("Name"), QStringLiteral("Сезон %1").arg(season)},
            {QStringLiteral("Type"), QStringLiteral("Season")},
            {QStringLiteral("IsFolder"), true},
            {QStringLiteral("SeriesId"), series.id},
            {QStringLiteral("SeriesName"), series.name},
            {QStringLiteral("IndexNumber"), season},
            {QStringLiteral("ImageTags"), QJsonObject{{QStringLiteral("Primary"), QStringLiteral("p")}}},
            {QStringLiteral("UserData"), userData(false, 0, false, episodesIn(series.id))}};
  }

  // Finds any item by id: a title, a season or an episode.
  std::optional<QJsonObject> itemById(const QString& id) {
    if (const Title* t = titleById(id)) return titleJson(*t);
    for (const Title& t : titles()) {
      if (t.type != QLatin1String("Series") || !id.startsWith(t.id)) continue;
      for (int s = 1; s <= seasonsOf(t.id); ++s) {
        if (id == QStringLiteral("%1s%2").arg(t.id).arg(s)) return seasonJson(t, s);
        for (int e = 1; e <= episodesIn(t.id); ++e)
          if (id == episodeId(t.id, s, e)) return episodeJson(t, s, e);
      }
    }
    return std::nullopt;
  }

  // --- Pictures ---------------------------------------------------------------

  QByteArray encode(const QImage& image, const char* format) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format, format == QByteArrayLiteral("JPEG") ? 85 : -1);
    return bytes;
  }

  // A landscape scene: sky gradient, a large soft sun or moon and layered
  // hills, all in the title's hue.
  QImage scene(int width, int height, int hue, size_t seed) {
    QImage image(width, height, QImage::Format_RGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, height);
    sky.setColorAt(0, QColor::fromHsv(hue, 120, 70));
    sky.setColorAt(0.6, QColor::fromHsv((hue + 20) % 360, 90, 150));
    sky.setColorAt(1, QColor::fromHsv((hue + 40) % 360, 60, 190));
    p.fillRect(image.rect(), sky);
    QRandomGenerator random(static_cast<quint32>(seed));
    const double cx = width * (0.55 + 0.3 * random.generateDouble());
    const double cy = height * (0.25 + 0.15 * random.generateDouble());
    QRadialGradient glow(cx, cy, height * 0.45);
    glow.setColorAt(0, QColor(255, 240, 220, 200));
    glow.setColorAt(0.15, QColor(255, 230, 200, 120));
    glow.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillRect(image.rect(), glow);
    for (int layer = 0; layer < 4; ++layer) {
      QPainterPath hills;
      const double base = height * (0.55 + 0.11 * layer);
      hills.moveTo(0, height);
      hills.lineTo(0, base);
      const double phase = random.generateDouble() * 6.28;
      for (int x = 0; x <= width; x += width / 40) {
        const double y = base - height * 0.08 * std::sin(phase + x * (2.0 + layer) / width * 3.14) -
                         height * 0.03 * std::sin(phase * 2 + x * 9.0 / width);
        hills.lineTo(x, y);
      }
      hills.lineTo(width, height);
      hills.closeSubpath();
      p.fillPath(hills, QColor::fromHsv((hue + 10 * layer) % 360, 110 + 20 * layer, 110 - 22 * layer));
    }
    return image;
  }

  QImage poster(const Title& t) {
    QImage image = scene(400, 600, t.hue, qHash(t.id));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient shade(0, 360, 0, 600);
    shade.setColorAt(0, QColor(0, 0, 0, 0));
    shade.setColorAt(1, QColor(0, 0, 0, 170));
    p.fillRect(0, 360, 400, 240, shade);
    QFont font(QStringLiteral("DejaVu Serif"), 34, QFont::Bold);
    p.setFont(font);
    p.setPen(QColor(250, 245, 235));
    p.drawText(QRect(24, 380, 352, 200), Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, t.name);
    return image;
  }

  QImage logo(const Title& t) {
    QFont font(QStringLiteral("DejaVu Serif"), 64, QFont::Black);
    font.setLetterSpacing(QFont::PercentageSpacing, 104);
    font.setCapitalization(QFont::AllUppercase);
    QImage image(1000, 320, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(font);
    QPainterPath text;
    const QStringList words = t.name.split(QLatin1Char(' '));
    // Two lines for a long title.
    const QString first = words.size() > 1 ? words.mid(0, (words.size() + 1) / 2).join(QLatin1Char(' ')) : t.name;
    const QString second = words.size() > 1 ? words.mid((words.size() + 1) / 2).join(QLatin1Char(' ')) : QString();
    text.addText(10, 120, font, first.toUpper());
    if (!second.isEmpty()) text.addText(10, 230, font, second.toUpper());
    QLinearGradient fill(0, 40, 0, 240);
    fill.setColorAt(0, QColor(255, 250, 240));
    fill.setColorAt(1, QColor::fromHsv(t.hue, 40, 225));
    p.fillPath(text, fill);
    p.end();
    const QRect used = text.boundingRect().toAlignedRect().adjusted(-6, -6, 6, 6).intersected(image.rect());
    return image.copy(used);
  }

  QImage episodeThumb(const QString& seriesId, int season, int episode) {
    const Title* t = titleById(seriesId);
    return scene(640, 360, (t ? t->hue : 200) + episode * 6, qHash(episodeId(seriesId, season, episode)));
  }

  test::HttpResponse image(const QString& path) {
    // Items/<id>/Images/<type>[/<index>]
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QString id = parts.value(1);
    const QString type = parts.value(3);
    static QHash<QString, QByteArray> cache;
    const QString key = id + QLatin1Char('/') + type;
    if (cache.contains(key)) return {200, cache.value(key), type == QLatin1String("Logo") ? "image/png" : "image/jpeg"};
    QByteArray bytes;
    const Title* t = titleById(id);
    if (type == QLatin1String("Logo") && t != nullptr) {
      bytes = encode(logo(*t), "PNG");
    } else if (type == QLatin1String("Backdrop") && t != nullptr) {
      bytes = encode(scene(1920, 1080, t->hue, qHash(t->id) + 7), "JPEG");
    } else if (t != nullptr) {
      bytes = encode(poster(*t), "JPEG");
    } else {
      // A season (poster of its series) or an episode (a still).
      for (const Title& series : titles()) {
        if (!id.startsWith(series.id)) continue;
        const qsizetype e = id.indexOf(QLatin1Char('e'), series.id.size());
        if (e > 0) {
          const QStringList se = id.mid(e + 1).split(QLatin1Char('x'));
          bytes = encode(episodeThumb(series.id, se.value(0).toInt(), se.value(1).toInt()), "JPEG");
        } else {
          bytes = encode(poster(series), "JPEG");
        }
        break;
      }
    }
    // A person: a soft portrait silhouette.
    if (bytes.isEmpty() && id.size() == 2 && id.startsWith(QLatin1Char('p'))) {
      const int n = id.mid(1).toInt();
      QImage picture(240, 240, QImage::Format_RGB32);
      QPainter p(&picture);
      p.setRenderHint(QPainter::Antialiasing);
      QLinearGradient back(0, 0, 0, 240);
      back.setColorAt(0, QColor::fromHsv((n * 47 + 180) % 360, 70, 120));
      back.setColorAt(1, QColor::fromHsv((n * 47 + 200) % 360, 90, 60));
      p.fillRect(picture.rect(), back);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor::fromHsv((n * 31 + 20) % 360, 40, 200));
      p.drawEllipse(QPointF(120, 100), 46, 54);
      p.drawEllipse(QPointF(120, 250), 95, 80);
      p.end();
      bytes = encode(picture, "JPEG");
    }
    // A library: a scene with its name.
    if (bytes.isEmpty() && (id == QLatin1String("movies") || id == QLatin1String("tv"))) {
      QImage picture = scene(640, 360, id == QLatin1String("tv") ? 215 : 280, qHash(id));
      QPainter p(&picture);
      p.setRenderHint(QPainter::Antialiasing);
      p.fillRect(picture.rect(), QColor(0, 0, 0, 90));
      p.setFont(QFont(QStringLiteral("DejaVu Sans"), 40, QFont::Bold));
      p.setPen(Qt::white);
      p.drawText(picture.rect(), Qt::AlignCenter, id == QLatin1String("tv") ? QStringLiteral("Сериалы") : QStringLiteral("Фильмы"));
      p.end();
      bytes = encode(picture, "JPEG");
    }
    if (bytes.isEmpty()) return {404, {}};
    cache.insert(key, bytes);
    return {200, bytes, type == QLatin1String("Logo") ? "image/png" : "image/jpeg"};
  }

  // --- The server -------------------------------------------------------------

  test::HttpResponse json(const QJsonValue& value) {
    const QByteArray body = value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                            : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    return test::HttpResponse{200, body, "application/json", 0};
  }

  test::HttpResponse page(const QJsonArray& items) {
    return json(QJsonObject{{QStringLiteral("Items"), items},
                            {QStringLiteral("TotalRecordCount"), items.size()},
                            {QStringLiteral("StartIndex"), 0}});
  }

  QJsonArray titlesIn(const QString& library) {
    QJsonArray out;
    for (const Title& t : titles())
      if (t.library == library) out.append(titleJson(t));
    return out;
  }

  // A query value, whatever the case of its name.
  QString queryValue(const test::HttpRequest& request, const QString& name) {
    for (const auto& [key, value] : request.query.queryItems())
      if (key.compare(name, Qt::CaseInsensitive) == 0) return value;
    return {};
  }

  test::HttpResponse handle(const test::HttpRequest& request) {
    const QString& path = request.path;
    if (path.contains(QLatin1String("/Images/"))) return image(path);
    if (path == QLatin1String("/UserViews")) {
      const auto view = [](const QString& id, const QString& name, const QString& type) {
        return QJsonObject{{QStringLiteral("Id"), id},
                           {QStringLiteral("Name"), name},
                           {QStringLiteral("Type"), QStringLiteral("CollectionFolder")},
                           {QStringLiteral("IsFolder"), true},
                           {QStringLiteral("ImageTags"), QJsonObject{{QStringLiteral("Primary"), QStringLiteral("p")}}},
                           {QStringLiteral("CollectionType"), type},
                           {QStringLiteral("ChildCount"), type == QLatin1String("movies") ? 7 : 3}};
      };
      return page(QJsonArray{view(QStringLiteral("movies"), QStringLiteral("Фильмы"), QStringLiteral("movies")),
                             view(QStringLiteral("tv"), QStringLiteral("Сериалы"), QStringLiteral("tvshows"))});
    }
    if (path == QLatin1String("/UserItems/Resume")) {
      return page(QJsonArray{episodeJson(*titleById(QStringLiteral("s1")), 1, 3), titleJson(*titleById(QStringLiteral("m1")))});
    }
    if (path == QLatin1String("/Shows/NextUp") && !queryValue(request, QStringLiteral("seriesId")).isEmpty()) {
      const Title* t = titleById(queryValue(request, QStringLiteral("seriesId")));
      return page(t ? QJsonArray{episodeJson(*t, 1, t->id == QLatin1String("s1") ? 3 : 1)} : QJsonArray{});
    }
    if (path == QLatin1String("/Shows/NextUp"))
      return page(QJsonArray{episodeJson(*titleById(QStringLiteral("s3")), 1, 1),
                             episodeJson(*titleById(QStringLiteral("s2")), 1, 1)});
    if (path == QLatin1String("/Items/Latest")) {
      const QString parent = queryValue(request, QStringLiteral("parentId"));
      return json(titlesIn(parent));
    }
    if (path.startsWith(QLatin1String("/Shows/")) && path.endsWith(QLatin1String("/Seasons"))) {
      const Title* t = titleById(path.section(QLatin1Char('/'), 2, 2));
      QJsonArray out;
      if (t)
        for (int s = 1; s <= seasonsOf(t->id); ++s) out.append(seasonJson(*t, s));
      return page(out);
    }
    if (path.startsWith(QLatin1String("/Shows/")) && path.endsWith(QLatin1String("/Episodes"))) {
      const Title* t = titleById(path.section(QLatin1Char('/'), 2, 2));
      const QString seasonId = queryValue(request, QStringLiteral("seasonId"));
      QJsonArray out;
      if (t)
        for (int s = 1; s <= seasonsOf(t->id); ++s) {
          if (!seasonId.isEmpty() && seasonId != QStringLiteral("%1s%2").arg(t->id).arg(s)) continue;
          for (int e = 1; e <= episodesIn(t->id); ++e) out.append(episodeJson(*t, s, e));
        }
      return page(out);
    }
    if (path.endsWith(QLatin1String("/Similar"))) {
      const QString id = path.section(QLatin1Char('/'), 2, 2);
      const Title* t = titleById(id);
      QJsonArray out;
      for (const Title& other : titles())
        if (t && other.id != id && other.type == t->type) out.append(titleJson(other));
      return page(out);
    }
    if (path.startsWith(QLatin1String("/Items/")) && path.count(QLatin1Char('/')) == 2) {
      const QString id = path.section(QLatin1Char('/'), 2, 2);
      if (id == QLatin1String("Filters"))
        return json(QJsonObject{{QStringLiteral("Genres"), QJsonArray{}}, {QStringLiteral("Years"), QJsonArray{}}});
      if (auto found = itemById(id)) return json(*found);
      return {404, {}};
    }
    if (path == QLatin1String("/Items")) {
      const QString parent = queryValue(request, QStringLiteral("parentId"));
      if (parent == QLatin1String("movies") || parent == QLatin1String("tv")) return page(titlesIn(parent));
      return page({});
    }
    return page({});
  }

  // An item by object name among the visual children (Repeater delegates
  // have no QObject parent, so findChild does not see them).
  QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (root == nullptr) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = findItem(child, name)) return found;
    return nullptr;
  }

  // The first card with quick actions under `root`.
  QQuickItem* findQuickCard(QQuickItem* root) {
    if (root == nullptr) return nullptr;
    if (root->property("quickActions").toBool() && root->isVisible()) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = findQuickCard(child)) return found;
    return nullptr;
  }

  // --- The window -------------------------------------------------------------

  class Showcase {
  public:
    Showcase() : server(handle) {
      ui::LibraryScreens::Config config;
      config.identity.version = QStringLiteral("0.0.1");
      config.identity.device = QStringLiteral("showcase");
      config.identity.deviceId = QStringLiteral("jfd-ui-showcase");
      config.serversFile = m_dir.filePath(QStringLiteral("servers.ini"));
      config.imageDirectory = m_dir.filePath(QStringLiteral("images"));
      config.tokens = std::make_unique<platform::MemoryCredentialStore>();
      screens = std::make_unique<ui::LibraryScreens>(std::move(config));
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      REQUIRE(navigator != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      // The settings screen, on the common sections.
      REQUIRE(settings::registerCommonSections(m_registry));
      m_settings = std::make_unique<settings::Settings>(m_registry, m_dir.filePath(QStringLiteral("settings.ini")));
      auto* appSettings = engine.singletonInstance<ui::AppSettings*>("Jfd.Ui.Settings", "AppSettings");
      REQUIRE(appSettings != nullptr);
      appSettings->attach(m_settings.get());
      navigator->registerPage(QStringLiteral("settings"),
                              QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Settings/SettingsPage.qml")));
      // Russian unless JFD_UI_LANG says otherwise (en: the source strings).
      const QString language = qEnvironmentVariable("JFD_UI_LANG", QStringLiteral("ru"));
      if (language != QLatin1String("en")) {
        const QString suffix = QLatin1Char('_') + language + QStringLiteral(".qm");
        for (const QString& file : QDir(QStringLiteral(":/i18n")).entryList({QLatin1Char('*') + suffix}, QDir::Files)) {
          auto* translator = new QTranslator(&m_translations);
          if (translator->load(QStringLiteral(":/i18n/") + file)) QCoreApplication::installTranslator(translator);
        }
        QLocale::setDefault(QLocale(language));
      }
      // The default look, as the application sets it (ThemeInstall).
      for (const char* weight : {"Regular", "Medium", "SemiBold", "Bold", "ExtraBold", "Black"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Jfd/Ui/Theme/fonts/Nunito-") +
                                          QLatin1String(weight) + QStringLiteral(".ttf"));
      QGuiApplication::setFont(QFont(QStringLiteral("Nunito")));
      QObject* theme = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme");
      REQUIRE(theme != nullptr);
      theme->setProperty("colors", ui::depthPalette());
      theme->setProperty("radiusOverride", 12);
      theme->setProperty("fontFamilyOverride", QStringLiteral("Nunito"));
      REQUIRE(screens->install(engine, *navigator));
      screens->ui()->setPlayHandler([](const QString&, qint64) {});
      api::JellyfinClient& client = screens->client();
      client.setServerUrl(server.url());
      client.setSession(QStringLiteral("token"), QStringLiteral("user"));
      screens->home().reload();
      REQUIRE(waitFor([&] { return screens->home().libraries()->count() == 2; }, Timeout));
      navigator->reset(QStringLiteral("home"));
      engine.loadData(R"(
        import QtQuick
        import Jfd.Ui.Shell
        import Jfd.Ui.Library
        Window {
            width: 1280
            height: 800
            visible: true
            AppShell {
                objectName: "shell"
                anchors.fill: parent
                accountName: "Анна"
                accountDetail: "Дом · jellyfin.local"
                onPaletteRequested: palette.toggle()
            }
            CommandPalette { id: palette; objectName: "palette"; anchors.fill: parent }
            Connections {
                target: CommandRegistry
                function onPaletteRequested() { palette.open() }
            }
        })");
      REQUIRE_FALSE(engine.rootObjects().isEmpty());
      window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
      REQUIRE(window != nullptr);
    }

    QQuickItem* page() const {
      auto* stack = window->findChild<QQuickItem*>(QStringLiteral("pageStack"));
      return stack == nullptr ? nullptr : stack->property("currentItem").value<QQuickItem*>();
    }

    QObject* find(const char* name) const {
      QQuickItem* current = page();
      return current == nullptr ? nullptr : current->findChild<QObject*>(QString::fromLatin1(name));
    }

    void resize(int width, int height) {
      window->resize(width, height);
      window->setProperty("width", width);
      window->setProperty("height", height);
      settle(400);
    }

    static void settle(int ms) {
      waitFor([] { return false; }, ms);
    }

    void screenshot(const QString& name) const {
      const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
      if (dir.isEmpty()) return;
      QDir().mkpath(dir);
      settle(900);
      window->grabWindow().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    ~Showcase() {
      for (auto* translator : m_translations.findChildren<QTranslator*>()) QCoreApplication::removeTranslator(translator);
    }

    QObject m_translations;
    QTemporaryDir m_dir;
    settings::SettingsRegistry m_registry;
    std::unique_ptr<settings::Settings> m_settings;
    test::FakeServer server;
    std::unique_ptr<ui::LibraryScreens> screens;
    QQmlApplicationEngine engine;
    ui::Navigator* navigator = nullptr;
    QQuickWindow* window = nullptr;
  };

} // namespace

TEST_CASE("Home: the banner features what is being watched, then the newest, with logos") {
  Showcase show;
  library::HomeModel& home = show.screens->home();
  REQUIRE(waitFor([&] { return home.featured()->count() == 5; }, Timeout));
  // The episode in progress first, standing for its series' logo.
  const auto* first = home.featured()->entry(0);
  CHECK(first->item.id == episodeId(QStringLiteral("s1"), 1, 3));
  CHECK_FALSE(first->backdropUrl.isEmpty());
  CHECK(first->logoUrl.toString().contains(QStringLiteral("Items/s1/Images/Logo")));
  // Then the film in progress and one next episode, then new items nobody
  // started; a series is not there twice.
  QStringList ids;
  QStringList reasons;
  for (int i = 0; i < home.featured()->count(); ++i) {
    ids.append(home.featured()->entry(i)->item.id);
    reasons.append(home.featured()->entry(i)->featuredReason);
  }
  CHECK(ids.at(1) == QStringLiteral("m1"));
  CHECK(reasons == QStringList{QStringLiteral("resume"), QStringLiteral("resume"), QStringLiteral("nextUp"),
                               QStringLiteral("latest"), QStringLiteral("latest")});
  CHECK_FALSE(ids.contains(QStringLiteral("s1")));
  for (int i = 3; i < home.featured()->count(); ++i) {
    const auto& data = home.featured()->entry(i)->item.userData;
    CHECK_FALSE(data.played);
    CHECK(data.playbackPositionTicks == 0);
  }

  REQUIRE(waitFor([&] { return show.find("homeHero") != nullptr && show.find("homeHero")->property("count").toInt() == 5; }, Timeout));
  QObject* hero = show.find("homeHero");
  CHECK(hero->property("current").toInt() == 0);
  // The page draws from the window's top, under a bar without a fill.
  CHECK((show.find("topNav") == nullptr || show.find("topNav")->property("transparent").toBool()));
  CHECK(waitFor([&] {
    QQuickItem* logo = findItem(findItem(qobject_cast<QQuickItem*>(hero), QStringLiteral("heroSlide_0")),
                                QStringLiteral("titleLogoImage"));
    return logo != nullptr && logo->property("status").toInt() == 1; // Image.Ready
  }, Timeout));
  QQuickItem* slide = findItem(qobject_cast<QQuickItem*>(hero), QStringLiteral("heroSlide_0"));
  QQuickItem* play = findItem(slide, QStringLiteral("heroPlay"));
  REQUIRE(play != nullptr);
  CHECK(play->property("text").toString().contains(QStringLiteral("12:40")));

  show.screenshot(QStringLiteral("home-1280"));
  show.resize(1600, 900);
  show.screenshot(QStringLiteral("home-1600"));
  show.resize(1920, 1080);
  show.screenshot(QStringLiteral("home-1920"));
  hero->setProperty("current", 1);
  show.screenshot(QStringLiteral("home-1920-movie"));
  show.resize(860, 700);
  show.screenshot(QStringLiteral("home-860"));
  show.resize(1280, 800);
  hero->setProperty("current", 0);

  // It goes on to the next item by itself (only while the app is active).
  show.window->requestActivate();
  REQUIRE(QTest::qWaitForWindowActive(show.window));
  REQUIRE(waitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, Timeout));
  CHECK_FALSE(hero->property("activeFocus").toBool());
  hero->setProperty("interval", 200);
  CHECK(waitFor([&] { return hero->property("current").toInt() == 1; }, Timeout));
  hero->setProperty("interval", 8000);
  hero->setProperty("current", 0);

  // A card's quick actions show on focus; the Menu key opens its menu.
  show.resize(1600, 900);
  QQuickItem* resume = findItem(show.page(), QStringLiteral("homeRow_resume"));
  REQUIRE(resume != nullptr);
  QQuickItem* card = nullptr;
  REQUIRE(waitFor([&] { return (card = findQuickCard(resume)) != nullptr; }, Timeout));
  show.find("homeRows")->setProperty("contentY", resume->y() - 120);
  Showcase::settle(300);
  card->forceActiveFocus(Qt::TabFocusReason);
  card->setProperty("focusReason", int(Qt::TabFocusReason));
  QQuickItem* quick = findItem(card, QStringLiteral("quickActions"));
  REQUIRE(quick != nullptr);
  CHECK(waitFor([&] { return quick->opacity() > 0.99; }, Timeout));
  CHECK(findItem(card, QStringLiteral("quickPlay"))->isVisible());
  show.screenshot(QStringLiteral("home-1600-card-focus"));
  QKeyEvent menuKey(QEvent::KeyPress, Qt::Key_Menu, Qt::NoModifier);
  QGuiApplication::sendEvent(show.window, &menuKey);
  QObject* menu = card->findChild<QObject*>(QStringLiteral("cardMenu"));
  REQUIRE(menu != nullptr);
  CHECK(waitFor([&] { return menu->property("opened").toBool(); }, Timeout));
  const QStringList entries = menu->property("entries").toStringList();
  CHECK(entries.contains(QStringLiteral("Перейти к сериалу")));
  CHECK(entries.contains(QStringLiteral("Смотреть с начала")));
  show.screenshot(QStringLiteral("home-1600-card-menu"));
  QMetaObject::invokeMethod(menu, "close");
}

TEST_CASE("Series, film and library pages on the demo catalogue") {
  Showcase show;
  REQUIRE(waitFor([&] { return show.screens->home().featured()->count() == 5; }, Timeout));

  show.navigator->navigate(QStringLiteral("item/s1"));
  CHECK(waitFor([&] {
    QObject* list = show.find("episodes");
    if (list) MESSAGE(list->property("count").toInt());
    return list != nullptr && list->property("count").toInt() == 8;
  }, Timeout));
  show.screenshot(QStringLiteral("series-1280"));
  show.resize(1600, 900);
  show.screenshot(QStringLiteral("series-1600"));
  show.resize(1920, 1080);
  show.screenshot(QStringLiteral("series-1920"));

  show.navigator->navigate(QStringLiteral("item/m1"));
  REQUIRE(waitFor([&] {
    QObject* name = show.find("itemName");
    return name != nullptr && !name->property("text").toString().isEmpty();
  }, Timeout));
  show.screenshot(QStringLiteral("movie-1920"));

  show.navigator->navigate(QStringLiteral("library/movies"));
  REQUIRE(waitFor([&] {
    QObject* grid = show.find("libraryGrid");
    return grid != nullptr && grid->property("count").toInt() == 7;
  }, Timeout));
  show.screenshot(QStringLiteral("library-1920"));

  // Large cards in a smaller window: the grid scrolls, so the letters show.
  auto* library = show.page()->property("library").value<QObject*>();
  REQUIRE(library != nullptr);
  library->setProperty("density", QStringLiteral("large"));
  show.resize(1280, 800);
  QQuickItem* bar = findItem(show.page(), QStringLiteral("alphabetBar"));
  REQUIRE(bar != nullptr);
  CHECK(waitFor([&] { return bar->isVisible(); }, Timeout));
  CHECK(findItem(bar, QStringLiteral("letter_Я")) != nullptr);
  show.screenshot(QStringLiteral("library-1280-large"));
  library->setProperty("density", QStringLiteral("medium"));

  // The Filters button shows the filters; those in use show as chips that
  // drop them.
  show.resize(1600, 900);
  library->setProperty("favoritesOnly", true);
  show.page()->setProperty("filtersOpen", true);
  QQuickItem* chip = nullptr;
  CHECK(waitFor([&] {
    chip = findItem(show.page(), QStringLiteral("filterChip_favorites"));
    return chip != nullptr && chip->isVisible();
  }, Timeout));
  CHECK(findItem(show.page(), QStringLiteral("libraryFilters"))->isVisible());
  show.screenshot(QStringLiteral("library-1600-filters"));
  REQUIRE(chip != nullptr);
  QMetaObject::invokeMethod(chip, "clicked");
  CHECK_FALSE(library->property("favoritesOnly").toBool());
  show.page()->setProperty("filtersOpen", false);
}

TEST_CASE("Settings: the search leaves the matching rows and marks the words") {
  Showcase show;
  show.navigator->navigate(QStringLiteral("settings"));
  REQUIRE(waitFor([&] { return show.find("settingsSearch") != nullptr; }, Timeout));
  QObject* list = show.find("settingsList");
  REQUIRE(list != nullptr);
  const int all = list->property("count").toInt();
  CHECK(all > 10);
  show.find("settingsSearch")->setProperty("text", QStringLiteral("цвет"));
  CHECK(waitFor([&] { return list->property("count").toInt() > 0 && list->property("count").toInt() < all; }, Timeout));
  show.resize(1600, 900);
  show.screenshot(QStringLiteral("settings-1600-search"));
  show.find("settingsSearch")->setProperty("text", QStringLiteral("zzzz"));
  CHECK(waitFor([&] { return list->property("count").toInt() == 0; }, Timeout));
  CHECK(show.find("settingsNothingFound")->property("visible").toBool());
}
