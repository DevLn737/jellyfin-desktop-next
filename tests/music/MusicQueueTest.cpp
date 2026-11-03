#include "../support/DoctestQt.h"

#include <jfd/music/MusicQueue.h>
#include <jfd/music/MusicQueueModel.h>

#include <QAbstractItemModelTester>
#include <QJsonDocument>

using namespace jfd::music;

namespace {
  QList<Track> tracks(int count, const char* prefix = "t") {
    QList<Track> out;
    for (int i = 0; i < count; ++i) {
      Track track;
      track.id = QStringLiteral("%1%2").arg(QLatin1String(prefix)).arg(i);
      track.name = track.id;
      out.append(track);
    }
    return out;
  }

  QStringList ids(const MusicQueue& queue) {
    QStringList out;
    for (const Track& track : queue.tracks())
      out.append(track.id);
    return out;
  }
} // namespace

TEST_CASE("A queue plays in order, then stops, repeats the queue, or repeats the track") {
  MusicQueue queue;
  queue.replace(tracks(3), 0);
  CHECK(queue.current() == 0);
  CHECK(queue.nextIndex(true) == 1);
  CHECK_FALSE(queue.previousIndex().has_value());

  queue.setCurrent(2);
  CHECK_FALSE(queue.nextIndex(true).has_value());
  queue.setRepeat(Repeat::Queue);
  CHECK(queue.nextIndex(true) == 0);
  queue.setCurrent(0);
  CHECK(queue.previousIndex() == 2);

  queue.setRepeat(Repeat::Track);
  // The track repeating itself is for when it ends; a skip goes on.
  CHECK(queue.nextIndex(true) == 0);
  CHECK(queue.nextIndex(false) == 1);
}

TEST_CASE("Entries are unique, also for the same track queued twice") {
  MusicQueue queue;
  queue.replace(tracks(2), 0);
  queue.append(tracks(2));
  CHECK(queue.size() == 4);
  CHECK(queue.tracks()[0].id == queue.tracks()[2].id);
  CHECK(queue.tracks()[0].entryId != queue.tracks()[2].entryId);
}

TEST_CASE("Play next goes right after the current track, in order") {
  MusicQueue queue;
  queue.replace(tracks(3), 1);
  queue.playNext(tracks(2, "n"));
  CHECK(ids(queue) == QStringList{QStringLiteral("t0"), QStringLiteral("t1"), QStringLiteral("n0"),
                                  QStringLiteral("n1"), QStringLiteral("t2")});
  CHECK(queue.current() == 1);
}

TEST_CASE("Removing and moving keep the current track") {
  MusicQueue queue;
  queue.replace(tracks(5), 2);
  CHECK(queue.remove(0) == MusicQueue::Removed::Other);
  CHECK(queue.current() == 1);
  CHECK(queue.currentTrack()->id == QStringLiteral("t2"));
  CHECK(queue.remove(3) == MusicQueue::Removed::Other);
  CHECK(queue.currentTrack()->id == QStringLiteral("t2"));

  queue.move(0, 2);
  CHECK(queue.currentTrack()->id == QStringLiteral("t2"));
  queue.move(1, 0);
  CHECK(queue.currentTrack()->id == QStringLiteral("t2"));

  // Removing the current one leaves current() on what followed it.
  const QString following = queue.tracks()[queue.current() + 1].id;
  CHECK(queue.remove(queue.current()) == MusicQueue::Removed::Current);
  CHECK(queue.currentTrack()->id == following);

  // The last one: current() falls back to the new last.
  queue.setCurrent(queue.size() - 1);
  CHECK(queue.remove(queue.current()) == MusicQueue::Removed::Current);
  CHECK(queue.current() == queue.size() - 1);

  queue.clear();
  CHECK(queue.current() == -1);
  CHECK(queue.isEmpty());
}

TEST_CASE("Shuffle mixes what follows, keeps the played tracks, and undoes itself") {
  MusicQueue queue;
  queue.seed(7);
  queue.replace(tracks(40), 5);
  const QStringList original = ids(queue);

  queue.setShuffle(true);
  CHECK(queue.current() == 5);
  CHECK(queue.currentTrack()->id == QStringLiteral("t5"));
  const QStringList shuffled = ids(queue);
  CHECK(shuffled.mid(0, 6) == original.mid(0, 6));
  CHECK(shuffled != original);
  QStringList sortedBefore = original, sortedAfter = shuffled;
  sortedBefore.sort();
  sortedAfter.sort();
  CHECK(sortedBefore == sortedAfter);

  // A track added while shuffled goes to the end and stays last when undone.
  queue.append(tracks(1, "late"));
  queue.setShuffle(false);
  CHECK(ids(queue).mid(0, 40) == original);
  CHECK(ids(queue).last() == QStringLiteral("late0"));
  CHECK(queue.currentTrack()->id == QStringLiteral("t5"));
}

TEST_CASE("Replacing a shuffled queue starts with the chosen track") {
  MusicQueue queue;
  queue.seed(3);
  queue.setShuffle(true);
  queue.replace(tracks(20), 9);
  CHECK(queue.current() == 0);
  CHECK(queue.currentTrack()->id == QStringLiteral("t9"));
  CHECK(queue.size() == 20);
}

TEST_CASE("A saved queue comes back as it was") {
  MusicQueue queue;
  queue.seed(1);
  queue.replace(tracks(6), 2);
  queue.setShuffle(true);
  queue.setRepeat(Repeat::Queue);
  QList<Track> extra = tracks(1, "g");
  extra[0].normalizationGain = -4.5;
  extra[0].runTimeTicks = 1234567;
  queue.append(extra);

  const QByteArray text = QJsonDocument(queue.toJson()).toJson(QJsonDocument::Compact);
  auto restored = MusicQueue::fromJson(QJsonDocument::fromJson(text).object());
  REQUIRE(restored.has_value());
  CHECK(ids(*restored) == ids(queue));
  CHECK(restored->current() == queue.current());
  CHECK(restored->shuffle());
  CHECK(restored->repeat() == Repeat::Queue);
  CHECK(restored->tracks().last().normalizationGain == doctest::Approx(-4.5));
  CHECK(restored->tracks().last().runTimeTicks == 1234567);

  // New entries do not collide with the restored ones, and shuffle undoes.
  restored->append(tracks(1, "z"));
  CHECK(restored->tracks().last().entryId > queue.tracks().last().entryId);
  restored->setShuffle(false);
  CHECK(restored->tracks()[restored->size() - 1].id == QStringLiteral("z0"));

  CHECK_FALSE(MusicQueue::fromJson(QJsonObject{}).has_value());
}

namespace {
  // What a list view holds, kept only from the model's signals: after each
  // edit it must match the queue.
  struct View {
    explicit View(MusicQueueModel& model) : m_model(model) {
      reload();
      QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [this] {
        ++resets;
        reload();
      });
      QObject::connect(&model, &QAbstractItemModel::rowsInserted, &model,
                       [this](const QModelIndex&, int first, int last) {
                         ++edits;
                         for (int row = first; row <= last; ++row) rows.insert(row, title(row));
                       });
      QObject::connect(&model, &QAbstractItemModel::rowsRemoved, &model,
                       [this](const QModelIndex&, int first, int last) {
                         ++edits;
                         rows.remove(first, last - first + 1);
                       });
      QObject::connect(&model, &QAbstractItemModel::rowsMoved, &model,
                       [this](const QModelIndex&, int first, int, const QModelIndex&, int destination) {
                         ++edits;
                         rows.move(first, destination > first ? destination - 1 : destination);
                       });
      QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                       [this](const QModelIndex& from, const QModelIndex& to) {
                         for (int row = from.row(); row <= to.row(); ++row)
                           if (m_model.data(m_model.index(row), MusicQueueModel::IsCurrent).toBool()) current = row;
                       });
    }

    QString title(int row) const { return m_model.data(m_model.index(row), MusicQueueModel::Title).toString(); }
    void reload() {
      rows.clear();
      for (int row = 0; row < m_model.rowCount(); ++row) rows.append(title(row));
    }

    MusicQueueModel& m_model;
    QStringList rows;
    int resets = 0;
    int edits = 0;
    int current = -1;
  };
} // namespace

TEST_CASE("Queue edits reach the list as row inserts, removals and moves, shuffled or not") {
  MusicQueue queue;
  queue.seed(7);
  MusicQueueModel model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model.setQueue(&queue);
  View view(model);

  for (const bool shuffled : {false, true}) {
    CAPTURE(shuffled);
    model.replace(tracks(6), 2);
    model.setShuffle(shuffled);
    const int resets = view.resets;
    CHECK(view.rows == ids(queue));

    model.append(tracks(2, "a"));
    CHECK(view.rows == ids(queue));
    CHECK(view.rows.mid(6) == QStringList{QStringLiteral("a0"), QStringLiteral("a1")});

    model.playNext(tracks(2, "n"));
    CHECK(view.rows == ids(queue));
    CHECK(view.rows.mid(queue.current() + 1, 2) == QStringList{QStringLiteral("n0"), QStringLiteral("n1")});

    // Down, up, and the current track itself.
    model.move(0, 5);
    CHECK(view.rows == ids(queue));
    model.move(7, 1);
    CHECK(view.rows == ids(queue));
    const QString playing = queue.currentTrack()->id;
    model.move(queue.current(), 0);
    CHECK(view.rows == ids(queue));
    CHECK(queue.currentTrack()->id == playing);
    model.move(3, 3);
    model.move(-1, 2);

    // After the current track, before it, and the current one: the track
    // that followed becomes current and its row says so.
    model.remove(queue.size() - 1);
    model.remove(0);
    CHECK(view.rows == ids(queue));
    model.remove(queue.current());
    CHECK(view.rows == ids(queue));
    CHECK(view.current == queue.current());
    model.remove(99);

    // None of these reset the list.
    CHECK(view.resets == resets);
    model.clear();
    CHECK(view.rows.isEmpty());
    CHECK(view.resets == resets);
  }
  CHECK(view.edits > 0);

  // A restored queue replaces everything at once.
  MusicQueue saved;
  saved.replace(tracks(3, "s"), 1);
  const int before = view.resets;
  model.assign(saved);
  CHECK(view.resets == before + 1);
  CHECK(view.rows == ids(queue));
}
