# Seerr API as the client uses it

Sources: the OpenAPI file `seerr-api.yml` of the Seerr project (version 1.0.0 of
the file; `main` and `develop` agree on everything below) and the answers of the
Seerr Docker image, collected by `scripts/test-server/seerr-probe.py` in the
"Seerr API" workflow. A line marked **spec** comes from the OpenAPI file only;
a line marked **seen** was observed on a running server (table at the end).

The address of a user's Seerr is a user setting. It does not appear in the
repository, in tests (they use `FakeSeerr`) or, masked as `***`, in logs and
reports (`addProtectedAddress`).

## Base and sign-in

- **spec** Base URL: `<server>/api/v1`. Two security schemes exist: the session
  cookie `connect.sid` and the header `X-Api-Key`.
- The API key is one per server and acts as the first administrator. The client
  never accepts, sends or stores a key: sign-in is only as the user's own
  Jellyfin account, so Seerr applies that user's permissions and limits.
- **spec** `POST /auth/jellyfin`, body `{"username", "password"}` (the optional
  `hostname`, `email`, `serverType` are not sent). Answer 200 with a `User` and a
  `Set-Cookie: connect.sid=...`. For a Jellyfin user that Seerr does not know
  yet, Seerr creates the account without permissions (so the first request may
  be refused until an administrator allows it).
- **spec** The `User` object carries `jellyfinAuthToken` and `plexToken`. The
  client reads only `id`, `username`, `permissions`, `requestCount` and throws
  the rest away; the redaction rule hides any `...Token` value in a log line.
- Before the password is sent: `GET /status` (version) and `GET /settings/public`
  (is it set up, which media server) must answer as a Seerr that uses Jellyfin.
  The password goes to this host only; a redirect to another host stops the
  request (no cookie, no body follows it).
- `GET /auth/me` checks that a stored session is alive; 401 or 403 means ask for
  the password again. `POST /auth/logout` ends the session.

## Endpoints used

| Purpose | Request | Notes |
| --- | --- | --- |
| Version | `GET /status` | no sign-in; `version`, `commitTag`, `updateAvailable` |
| Server kind | `GET /settings/public` | no sign-in; **seen** `mediaServerType` (4 on a server with no media server set up; 2 is Jellyfin, the only value the client signs in to), `initialized`, `mediaServerLogin`, `localLogin`, `partialRequestsEnabled`, `movie4kEnabled`, `series4kEnabled`, `applicationTitle`, `jellyfinExternalHost` (not in the OpenAPI file) |
| Popular | `GET /discover/movies`, `GET /discover/tv` (`page`, `language`) | paged: `page`, `totalPages`, `totalResults`, `results[]` |
| Trending | `GET /discover/trending` (`page`, `mediaType`, `timeWindow`) | results mix `movie`, `tv`, `person`; the client drops `person` |
| Search | `GET /search?query=...&page=` | same result shapes |
| Movie page | `GET /movie/{tmdbId}` | `mediaInfo` is absent when Seerr has never seen the title |
| Series page | `GET /tv/{tmdbId}` | `seasons[]` with `seasonNumber`, `episodeCount`; season 0 is specials |
| My requests | `GET /user/{id}/requests?take=&skip=` | `pageInfo`, `results[]` of `MediaRequest` |
| Request | `POST /request` | body `{"mediaType":"movie"\|"tv","mediaId":<tmdbId>,"seasons":[n,...]\|"all"}`; answer 201 with the `MediaRequest`. `is4k`, `serverId`, `profileId`, `rootFolder`, `userId`, `ignoreQuota` are never sent: Seerr's defaults apply |
| Cancel | `DELETE /request/{id}` | answer 204; only the user's own request |

## Status values

- **spec** Media (`mediaInfo.status`): 1 unknown, 2 pending, 3 processing,
  4 partially available, 5 available, 6 deleted. No `mediaInfo` means "not
  requested".
- **spec** Request (`status`): 1 pending approval, 2 approved, 3 declined.
- Shown to the user as text and an icon, never colour alone. Available titles
  link to the page of the same title in the client when Jellyfin has it.

## Refusals the client must handle

Signed out or expired session: **seen** 401 with
`{"message":"cookie 'connect.sid' required", ...}` on any call below `/status`
and `/settings/public` (the client treats 403 the same way). A wrong `X-Api-Key`
is 403. No right to request: 403 on `POST /request`; a quota or a
duplicate request: 4xx with a `message` field. Every refusal becomes a plain
sentence in the user's language; the response body is never shown raw and never
logged.

## In the app

- **Setting up.** Settings > Requests (Seerr) has the address, "Sign in to
  Seerr", "Check the connection" and "Sign out of Seerr". The address is the
  user's own setting and is kept out of logs and diagnostic reports
  (`addProtectedAddress`). Empty: no "Requests" in the sidebar, no row on the
  home page, nothing sent anywhere.
- **Signing in.** The sign-in page shows the server's host and says the
  Jellyfin password goes to that Seerr server, not to the Jellyfin server; the
  password is sent only after a confirmation, and only to a server whose
  `/status` and `/settings/public` say it is initialised with Jellyfin as its
  media server. A plain `http` address outside the local network gets a
  warning on the page. The password is not kept; the session is the
  `connect.sid` cookie, stored in the platform credential storage under the
  address (a different address has its own session) and dropped when Seerr
  answers 401.
- **Screens.** `requests` (sidebar; trending, popular films and series,
  search), `seerr-title/<movie|tv>:<tmdbId>` (facts, status, seasons,
  Request), `requests-mine` (own requests, cancel while waiting for approval)
  and `seerr-signin`. Nothing is requested without a question first; all
  seasons of a series need a question of their own. Statuses are words and a
  symbol, never colour alone.
- **Posters** come from the TMDB image CDN (`image.tmdb.org`), as Seerr's own
  pages do, through the app's image cache. They are fetched without any
  cookie.
- **Tests.** `tests/seerr` runs the client, the service and the models against
  `tests/support/FakeSeerr.h`; `tests/ui/RequestsPageTest.cpp` drives the
  screens in an offscreen window. Nothing in the repository names a real
  Seerr server.

## Seen on a running server

Seerr 3.5.1 (image `ghcr.io/seerr-team/seerr` at digest `sha256:d8fab675b55ad293ded98fd71efcfd46054cbad9746534ae1cc4794a88c0b3be`, workflow "Seerr API"),
a fresh server with no media server set up, no sign-in:

| Request | Status | Note |
| --- | --- | --- |
| `GET /status` | 200 | keys `version`, `commitTag`, `updateAvailable`, `commitsBehind`, `restartRequired` |
| `GET /settings/public` | 200 | `mediaServerType` is 4 |
| `GET /auth/me`, `/discover/trending`, `/search`, `/request`, `/movie/{id}` without a session | 401 | `cookie 'connect.sid' required` |
| `GET /auth/me` with a wrong `X-Api-Key` | 403 | |
| `POST /auth/jellyfin` with a wrong user, server not set up | 500 | no `Set-Cookie`; the client reads any non-200 as "sign-in failed" |
| `POST /auth/logout` without a session | 401 | |

Not covered here (needs a Jellyfin behind Seerr, added with the client's live
test): the `Set-Cookie` of a good sign-in and its attributes, the `User`
object, `permissions` values, and the refusals of `POST /request`.
