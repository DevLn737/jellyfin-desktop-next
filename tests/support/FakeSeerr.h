#pragma once

#include "FakeServer.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

// A Seerr on localhost for offline tests of the client: the answers
// docs/seerr.md lists (paths, status codes, the connect.sid cookie), with
// users, sessions, a few titles and requests. Answers carry no real data.
namespace jfd::test {

  class FakeSeerr {
  public:
    struct Account {
      int id = 0;
      QString password;
      bool canRequest = true;
      int quotaLeft = 10;
    };

    FakeSeerr() : m_server([this](const HttpRequest& r) { return answer(r); }) {
      m_accounts.insert(QStringLiteral("alice"), Account{7, QStringLiteral("pw-alice"), true, 10});
      m_accounts.insert(QStringLiteral("viewer"), Account{8, QStringLiteral("pw-viewer"), false, 10});
    }

    FakeServer& server() { return m_server; }
    const FakeServer& server() const { return m_server; }
    QUrl url() const { return m_server.url(); }

    // Knobs.
    int mediaServerType = 2;
    bool initialized = true;
    bool loginFails500 = false;
    // When set, every /api/v1/status answer redirects there.
    QString redirectStatusTo;
    // Seerr's Jellyfin token for the user, which must never leak anywhere.
    QString jellyfinToken = QStringLiteral("JF-TOKEN-SECRET");

    QMap<QString, Account>& accounts() { return m_accounts; }
    QSet<QString>& sessions() { return m_sessions; }
    const QList<QJsonObject>& requests() const { return m_requests; }

    static QJsonObject movie(int id, const QString& title, int status = 0) {
      QJsonObject o{{"id", id}, {"mediaType", "movie"}, {"title", title},
                    {"releaseDate", "2021-03-04"}, {"posterPath", "/p.jpg"}, {"voteAverage", 7.5}};
      if (status) o.insert("mediaInfo", QJsonObject{{"id", 1}, {"status", status}});
      return o;
    }
    static QJsonObject show(int id, const QString& name, int status = 0) {
      QJsonObject o{{"id", id}, {"mediaType", "tv"}, {"name", name}, {"firstAirDate", "2019-09-01"}};
      if (status) o.insert("mediaInfo", QJsonObject{{"id", 2}, {"status", status}});
      return o;
    }

  private:
    static HttpResponse reply(const QJsonObject& o, int status = 200) {
      return HttpResponse{status, QJsonDocument(o).toJson(QJsonDocument::Compact), "application/json", 0};
    }
    static HttpResponse error(int status, const char* message = "refused") {
      return reply(QJsonObject{{"message", message}}, status);
    }

    QJsonObject page(const QJsonArray& results) const {
      return QJsonObject{{"page", 1}, {"totalPages", 2}, {"totalResults", 3 * results.size()},
                         {"results", results}};
    }

    int sessionUser(const HttpRequest& r) const {
      const QByteArray cookie = r.headers.value("cookie");
      const QByteArray prefix = "connect.sid=";
      const qsizetype at = cookie.indexOf(prefix);
      if (at < 0) return 0;
      const QString value = QString::fromUtf8(cookie.mid(at + prefix.size()).split(';').value(0));
      if (!m_sessions.contains(value)) return 0;
      return value.mid(value.indexOf(QLatin1Char('-')) + 1).toInt();
    }

    HttpResponse answer(const HttpRequest& r) {
      const QString path = r.path;
      if (!path.startsWith(QLatin1String("/api/v1"))) return error(404);
      const QString route = path.mid(7);
      const bool post = r.method == "POST";

      if (route == QLatin1String("/status")) {
        if (!redirectStatusTo.isEmpty()) {
          HttpResponse out{302, {}, {}, 0};
          out.headers.append({"Location", redirectStatusTo.toUtf8()});
          return out;
        }
        return reply(QJsonObject{{"version", "3.5.1"}, {"updateAvailable", false}});
      }
      if (route == QLatin1String("/settings/public"))
        return reply(QJsonObject{{"initialized", initialized}, {"mediaServerType", mediaServerType},
                                 {"partialRequestsEnabled", true}});

      if (route == QLatin1String("/auth/jellyfin") && post) {
        if (loginFails500) return error(500, "boom");
        const QJsonObject body = QJsonDocument::fromJson(r.body).object();
        const QString name = body.value("username").toString();
        const auto it = m_accounts.constFind(name);
        if (it == m_accounts.constEnd() || it->password != body.value("password").toString())
          return error(401, "Invalid credentials");
        const QString cookie = QStringLiteral("s%3Atoken.%1-%2").arg(m_sessions.size() + 1).arg(it->id);
        const QString value = QStringLiteral("s%3Atoken.%1-%2").arg(m_sessions.size() + 1).arg(it->id);
        m_sessions.insert(cookie);
        HttpResponse out = reply(userJson(name, *it));
        out.headers.append({"Set-Cookie", ("connect.sid=" + value + "; Path=/; HttpOnly; SameSite=Lax").toUtf8()});
        return out;
      }

      const int userId = sessionUser(r);
      if (userId == 0) return error(401, "cookie 'connect.sid' required");

      if (route == QLatin1String("/auth/logout") && post) {
        m_sessions.clear();
        return reply({});
      }
      QString name;
      Account account;
      for (auto it = m_accounts.cbegin(); it != m_accounts.cend(); ++it) {
        if (it->id == userId) {
          name = it.key();
          account = *it;
        }
      }
      if (route == QLatin1String("/auth/me")) return reply(userJson(name, account));

      if (route == QLatin1String("/discover/movies"))
        return reply(page({movie(603, "The Matrix", 5), movie(604, "Reloaded")}));
      if (route == QLatin1String("/discover/tv"))
        return reply(page({show(1399, "Thrones", 4), show(1400, "Other")}));
      if (route == QLatin1String("/discover/trending"))
        return reply(page({movie(603, "The Matrix", 5), show(1399, "Thrones", 2),
                           QJsonObject{{"id", 5}, {"mediaType", "person"}, {"name", "Someone"}}}));
      if (route == QLatin1String("/search")) {
        const QString text = QUrl::fromPercentEncoding(r.query.queryItemValue(QStringLiteral("query")).toUtf8());
        if (text.isEmpty()) return error(400, "query required");
        return reply(page({movie(603, "Found " + text)}));
      }
      if (route == QLatin1String("/movie/603"))
        return reply(QJsonObject{{"id", 603}, {"title", "The Matrix"}, {"overview", "Neo."},
                                 {"releaseDate", "1999-03-31"}, {"runtime", 136}, {"tagline", "Free your mind"},
                                 {"genres", QJsonArray{QJsonObject{{"id", 28}, {"name", "Action"}}}},
                                 {"mediaInfo", QJsonObject{{"status", 5}}}});
      if (route == QLatin1String("/tv/1399"))
        return reply(QJsonObject{
            {"id", 1399}, {"name", "Thrones"}, {"firstAirDate", "2011-04-17"},
            {"episodeRunTime", QJsonArray{55}},
            {"seasons", QJsonArray{QJsonObject{{"seasonNumber", 0}, {"episodeCount", 3}, {"name", "Specials"}},
                                   QJsonObject{{"seasonNumber", 1}, {"episodeCount", 10}, {"name", "Season 1"}},
                                   QJsonObject{{"seasonNumber", 2}, {"episodeCount", 10}, {"name", "Season 2"}}}},
            {"mediaInfo", QJsonObject{{"status", 4},
                                      {"seasons", QJsonArray{QJsonObject{{"seasonNumber", 1}, {"status", 5}}}},
                                      {"requests", QJsonArray{QJsonObject{{"id", 31}, {"status", 2},
                                                                          {"requestedBy", QJsonObject{{"id", 7}}}}}}}}});

      if (route == QLatin1String("/request") && post) {
        if (!account.canRequest) return error(403, "You do not have permission to make requests");
        const QJsonObject body = QJsonDocument::fromJson(r.body).object();
        const int tmdb = body.value("mediaId").toInt();
        for (const QJsonObject& existing : m_requests) {
          if (existing.value("media").toObject().value("tmdbId").toInt() == tmdb)
            return error(409, "Request for this media already exists");
        }
        if (account.quotaLeft <= 0) return error(403, "Quota exceeded");
        const QJsonObject made{{"id", 100 + m_requests.size()}, {"status", 1}, {"type", body.value("mediaType")},
                               {"requestedBy", QJsonObject{{"id", userId}}},
                               {"media", QJsonObject{{"tmdbId", tmdb}, {"mediaType", body.value("mediaType")},
                                                     {"status", 2}}},
                               {"seasons", body.value("seasons").isArray() ? seasonObjects(body.value("seasons").toArray()) : QJsonArray()}};
        m_requests.append(made);
        m_lastRequestBody = body;
        return reply(made, 201);
      }
      if (route.startsWith(QLatin1String("/request/")) && r.method == "DELETE") {
        const int id = route.mid(9).toInt();
        for (qsizetype i = 0; i < m_requests.size(); ++i) {
          if (m_requests[i].value("id").toInt() == id) {
            m_requests.removeAt(i);
            return HttpResponse{204, {}, {}, 0};
          }
        }
        return error(404, "Request not found");
      }
      if (route == QStringLiteral("/user/%1/requests").arg(userId)) {
        QJsonArray list;
        for (const QJsonObject& request : m_requests)
          list.append(request);
        return reply(QJsonObject{{"pageInfo", QJsonObject{{"results", list.size()}}}, {"results", list}});
      }
      return error(404);
    }

    static QJsonArray seasonObjects(const QJsonArray& numbers) {
      QJsonArray out;
      for (const QJsonValue& n : numbers)
        out.append(QJsonObject{{"seasonNumber", n.toInt()}});
      return out;
    }

    QJsonObject userJson(const QString& name, const Account& a) const {
      return QJsonObject{{"id", a.id}, {"displayName", name}, {"permissions", a.canRequest ? 32 : 0},
                         {"requestCount", m_requests.size()}, {"jellyfinAuthToken", jellyfinToken},
                         {"plexToken", QJsonValue::Null}};
    }

  public:
    QJsonObject lastRequestBody() const { return m_lastRequestBody; }

  private:
    FakeServer m_server;
    QMap<QString, Account> m_accounts;
    QSet<QString> m_sessions;
    QList<QJsonObject> m_requests;
    QJsonObject m_lastRequestBody;
  };

} // namespace jfd::test
