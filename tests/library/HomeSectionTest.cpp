#include "../support/DoctestQt.h"

#include <jfd/library/HomeSection.h>

#include <QSignalSpy>
#include <QStringListModel>

using namespace jfd::library;

namespace {

  HomeSection section(const char* id, int order) {
    HomeSection s;
    s.id = QString::fromLatin1(id);
    s.title = s.id;
    s.defaultOrder = order;
    return s;
  }

} // namespace

TEST_CASE("Home sections come in default order, then in the user's order") {
  HomeSections sections;
  QSignalSpy changes(&sections, &HomeSections::changed);
  sections.add(section("latest/movies", 30));
  sections.add(section("resume", 10));
  sections.add(section("seerr.popular", 90));
  sections.add(section("nextUp", 20));
  CHECK(changes.count() == 4);

  QStringList defaults;
  for (const HomeSection* s : sections.byDefaultOrder()) defaults << s->id;
  CHECK(defaults == QStringList{QStringLiteral("resume"), QStringLiteral("nextUp"), QStringLiteral("latest/movies"),
                                QStringLiteral("seerr.popular")});

  // No arrangement: the default order.
  CHECK(sections.displayOrder({}) == defaults);
  // The user's list wins; ids that are not registered (a removed module,
  // another server's library) and repeats are skipped; sections the list
  // does not know (a new module) follow in default order.
  CHECK(sections.displayOrder({QStringLiteral("seerr.popular"), QStringLiteral("gone"), QStringLiteral("resume"),
                               QStringLiteral("seerr.popular")}) ==
        QStringList{QStringLiteral("seerr.popular"), QStringLiteral("resume"), QStringLiteral("nextUp"),
                    QStringLiteral("latest/movies")});
}

TEST_CASE("A section is replaced by id and removed once") {
  HomeSections sections;
  QStringListModel model;
  HomeSection s = section("music.recent", 50);
  s.model = &model;
  int reloads = 0;
  s.reload = [&] { ++reloads; };
  sections.add(s);
  sections.add(section("", 1));
  CHECK(sections.find(QStringLiteral("music.recent"))->model == &model);
  sections.find(QStringLiteral("music.recent"))->reload();
  CHECK(reloads == 1);

  HomeSection renamed = section("music.recent", 5);
  renamed.title = QStringLiteral("Recently played");
  sections.add(renamed);
  CHECK(sections.byDefaultOrder().size() == 1);
  CHECK(sections.find(QStringLiteral("music.recent"))->title == QStringLiteral("Recently played"));

  QSignalSpy changes(&sections, &HomeSections::changed);
  sections.remove(QStringLiteral("music.recent"));
  sections.remove(QStringLiteral("music.recent"));
  CHECK(changes.count() == 1);
  CHECK_FALSE(sections.contains(QStringLiteral("music.recent")));
}

TEST_CASE("A section's model goes away with its owner without a crash") {
  HomeSections sections;
  {
    QStringListModel model;
    HomeSection s = section("music.recent", 50);
    s.model = &model;
    sections.add(s);
  }
  CHECK(sections.find(QStringLiteral("music.recent"))->model.isNull());
}

TEST_CASE("The arrangement is read and written as a list with the hidden ones marked") {
  const HomeArrangement parsed = parseArrangement(QStringLiteral(R"(["resume","-nextUp","latest/abc","resume",""])"));
  CHECK(parsed.order == QStringList{QStringLiteral("resume"), QStringLiteral("nextUp"), QStringLiteral("latest/abc")});
  CHECK(parsed.hidden == QStringList{QStringLiteral("nextUp")});
  CHECK(parseArrangement(formatArrangement(parsed)) == parsed);
  CHECK(formatArrangement(parsed) == QStringLiteral(R"(["resume","-nextUp","latest/abc"])"));
}

TEST_CASE("Anything that is not a list gives no arrangement") {
  const HomeArrangement empty;
  CHECK(parseArrangement(QString()) == empty);
  CHECK(parseArrangement(QStringLiteral("{\"a\":1}")) == empty);
  CHECK(parseArrangement(QStringLiteral("not json")) == empty);
  CHECK(parseArrangement(QStringLiteral("[1,null,{}]")) == empty);
}

TEST_CASE("The saved order skips unknown sections and puts new ones last") {
  HomeSections sections;
  sections.add(section("resume", 10));
  sections.add(section("nextUp", 20));
  sections.add(section("seerr.popular", 90));
  const QStringList order = sections.displayOrder(QStringList{QStringLiteral("gone"), QStringLiteral("nextUp"), QStringLiteral("resume"), QStringLiteral("nextUp")});
  CHECK(order == QStringList{QStringLiteral("nextUp"), QStringLiteral("resume"), QStringLiteral("seerr.popular")});
}
