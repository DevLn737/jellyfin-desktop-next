#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/remote/RemoteControl.h>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUrlQuery>

#include <memory>
#include <optional>
#include <vector>

// Remote control against the live server started by scripts/test-server
// (JFD_TEST_SERVER_URL, JFD_TEST_USER, JFD_TEST_PASSWORD): a second session
// of the same user sends what Jellyfin Web's "Play on" sends, and this
// client's remote control carries it out within a second.

using namespace jfd;
using namespace jfd::api;
using jfd::test::waitFor;

namespace {
  constexpr int CommandBudgetMs = 1000;

  template <class T> Result<T> run(std::function<void(Callback<T>)> call, int timeoutMs = 20000) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    if (!waitFor([&] { return out.has_value(); }, timeoutMs)) {
      ApiError error;
      error.kind = ApiError::Kind::Timeout;
      error.message = QStringLiteral("test timed out");
      return error;
    }
    return std::move(*out);
  }

  std::unique_ptr<JellyfinClient> signedIn(const QString& deviceId) {
    auto client = std::make_unique<JellyfinClient>(ClientIdentity{QStringLiteral("Jellyfin Desktop Next Tests"),
                                                                  QStringLiteral("0.0.1"),
                                                                  QStringLiteral("CI ") + deviceId, deviceId});
    const auto url = JellyfinClient::normalizeServerUrl(qEnvironmentVariable("JFD_TEST_SERVER_URL"));
    REQUIRE_MESSAGE(url.has_value(), "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
    client->setServerUrl(*url);
    auto auth = run<AuthenticationResult>([&](auto done) {
      client->signIn(qEnvironmentVariable("JFD_TEST_USER"), qEnvironmentVariable("JFD_TEST_PASSWORD"), nullptr, done);
    });
    REQUIRE_MESSAGE(auth.ok(), (auth.ok() ? QString() : auth.error().message).toStdString());
    return client;
  }

  QJsonObject sessionOf(JellyfinClient& client, const QString& deviceId) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("deviceId"), deviceId);
    auto sessions = run<JellyfinClient::RawResponse>([&](auto done) {
      client.send(JellyfinClient::Method::Get, QStringLiteral("Sessions"), query, std::nullopt, nullptr, done);
    });
    REQUIRE(sessions.ok());
    const QJsonArray list = QJsonDocument::fromJson(sessions->body).array();
    REQUIRE(list.size() == 1);
    return list.at(0).toObject();
  }

  QString anyMovie(JellyfinClient& client) {
    ItemQuery query;
    query.includeItemTypes = {QStringLiteral("Movie")};
    auto page = run<ItemPage>([&](auto done) { client.items(query, nullptr, done); });
    REQUIRE(page.ok());
    REQUIRE_FALSE(page->items.isEmpty());
    return page->items.front().id;
  }
} // namespace

TEST_CASE("Play, Playstate and GeneralCommand from another session are carried out") {
  // The controlled client: what its remote control did.
  std::vector<core::SessionInput> posted;
  std::vector<remote::PlayRequest> plays;
  int volume = 100;
  QStringList messages;
  core::SessionSnapshot snapshot;
  snapshot.phase = core::PlaybackPhase::Playing;
  snapshot.item = core::ItemId("playing");
  snapshot.duration = core::MediaTicks::fromMilliseconds(3'600'000);

  remote::RemoteActions actions;
  actions.play = [&](const remote::PlayRequest& request) { plays.push_back(request); };
  actions.volume = [&] { return volume; };
  actions.setVolume = [&](int value) { volume = value; };
  actions.showMessage = [&](const QString& header, const QString& text) {
    messages << header + QLatin1Char('|') + text;
  };
  remote::RemoteControl control([&](core::SessionInput input) { posted.push_back(std::move(input)); },
                                [&] { return snapshot; }, actions);

  const QString targetDevice = QStringLiteral("jfd-remote-target");
  auto target = signedIn(targetDevice);
  SessionCapabilities capabilities;
  capabilities.deviceProfile = buildDeviceProfile({});
  control.applyTo(capabilities);
  REQUIRE(run<Empty>([&](auto done) { target->reportCapabilities(capabilities, nullptr, done); }).ok());
  SessionSocket socket(*target);
  QObject::connect(
      &socket, &SessionSocket::messageReceived, &control,
      [&control](const QString& type, const QJsonValue& data, const QString&) { control.handle(type, data); });
  QSignalSpy ready(&socket, &SessionSocket::ready);
  socket.start();
  REQUIRE(waitFor([&] { return ready.count() == 1; }, 20000));

  auto controller = signedIn(QStringLiteral("jfd-remote-controller"));
  const QJsonObject session = sessionOf(*controller, targetDevice);
  const QString id = session.value(QStringLiteral("Id")).toString();
  REQUIRE_FALSE(id.isEmpty());
  // The server offers the client as a target of "Play on".
  CHECK(session.value(QStringLiteral("SupportsRemoteControl")).toBool());
  const QJsonObject registered = session.value(QStringLiteral("Capabilities")).toObject();
  CHECK(registered.value(QStringLiteral("SupportsMediaControl")).toBool());
  CHECK(registered.value(QStringLiteral("SupportedCommands")).toArray().contains(QStringLiteral("SetVolume")));

  const auto post = [&](const QString& path, const QUrlQuery& query, const std::optional<QJsonDocument>& body) {
    REQUIRE(run<Empty>([&](auto done) {
              controller->sendNoContent(JellyfinClient::Method::Post, path, query, body, nullptr, done);
            }).ok());
  };
  QElapsedTimer timer;

  timer.start();
  post(QStringLiteral("Sessions/%1/Playing/Pause").arg(id), {}, std::nullopt);
  REQUIRE(waitFor([&] { return !posted.empty(); }, CommandBudgetMs));
  CHECK(std::holds_alternative<core::intent::Pause>(posted.back()));
  CHECK(timer.elapsed() < CommandBudgetMs);

  QUrlQuery seek;
  seek.addQueryItem(QStringLiteral("seekPositionTicks"), QStringLiteral("600000000"));
  timer.restart();
  post(QStringLiteral("Sessions/%1/Playing/Seek").arg(id), seek, std::nullopt);
  REQUIRE(waitFor([&] { return posted.size() == 2; }, CommandBudgetMs));
  REQUIRE(std::holds_alternative<core::intent::Seek>(posted.back()));
  CHECK(std::get<core::intent::Seek>(posted.back()).position.milliseconds() == 60'000);

  timer.restart();
  post(QStringLiteral("Sessions/%1/Command").arg(id), {},
       QJsonDocument(
           QJsonObject{{QStringLiteral("Name"), QStringLiteral("SetVolume")},
                       {QStringLiteral("Arguments"), QJsonObject{{QStringLiteral("Volume"), QStringLiteral("30")}}}}));
  REQUIRE(waitFor([&] { return volume == 30; }, CommandBudgetMs));

  post(QStringLiteral("Sessions/%1/Message").arg(id), {},
       QJsonDocument(QJsonObject{{QStringLiteral("Header"), QStringLiteral("Hello")},
                                 {QStringLiteral("Text"), QStringLiteral("From the phone")},
                                 {QStringLiteral("TimeoutMs"), 5000}}));
  REQUIRE(waitFor([&] { return !messages.isEmpty(); }, CommandBudgetMs));
  CHECK(messages.front() == QStringLiteral("Hello|From the phone"));

  const QString movie = anyMovie(*controller);
  QUrlQuery play;
  play.addQueryItem(QStringLiteral("playCommand"), QStringLiteral("PlayNow"));
  play.addQueryItem(QStringLiteral("itemIds"), movie);
  play.addQueryItem(QStringLiteral("startPositionTicks"), QStringLiteral("100000000"));
  timer.restart();
  post(QStringLiteral("Sessions/%1/Playing").arg(id), play, std::nullopt);
  REQUIRE(waitFor([&] { return !plays.empty(); }, CommandBudgetMs));
  CHECK(timer.elapsed() < CommandBudgetMs);
  CHECK(plays.front().command == remote::PlayCommand::PlayNow);
  CHECK(plays.front().itemIds == QStringList{movie});
  CHECK(plays.front().startPositionTicks == 100'000'000);

  socket.stop();
}

TEST_CASE("Turned off, the client is not offered for remote control") {
  remote::RemoteControl control([](core::SessionInput) {}, [] { return core::SessionSnapshot{}; }, {});
  control.setEnabled(false);
  const QString device = QStringLiteral("jfd-remote-off");
  auto client = signedIn(device);
  SessionCapabilities capabilities;
  capabilities.deviceProfile = buildDeviceProfile({});
  control.applyTo(capabilities);
  REQUIRE(run<Empty>([&](auto done) { client->reportCapabilities(capabilities, nullptr, done); }).ok());
  const QJsonObject registered = sessionOf(*client, device).value(QStringLiteral("Capabilities")).toObject();
  CHECK_FALSE(registered.value(QStringLiteral("SupportsMediaControl")).toBool());
  CHECK(registered.value(QStringLiteral("SupportedCommands")).toArray().isEmpty());
}
