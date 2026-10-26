#!/usr/bin/env python3
"""Asks a running Seerr (no sign-in, no key) the questions docs/seerr.md answers.

Usage: seerr-probe.py http://127.0.0.1:5055

Prints a Markdown table of what the server really answers (status code, the
headers and body keys that matter) and exits 1 when an answer differs from
what the client relies on. Nothing here sends a real credential.
"""
import json
import sys
import urllib.error
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def call(base, method, path, body=None, headers=None):
    data = None
    hdrs = {"Accept": "application/json"}
    hdrs.update(headers or {})
    if body is not None:
        data = json.dumps(body).encode()
        hdrs["Content-Type"] = "application/json"
    req = urllib.request.Request(base + "/api/v1" + path, data=data, method=method, headers=hdrs)
    try:
        resp = OPENER.open(req, timeout=20)
    except urllib.error.HTTPError as err:
        resp = err
    raw = resp.read()
    try:
        parsed = json.loads(raw)
    except ValueError:
        parsed = None
    return resp.status if hasattr(resp, "status") else resp.code, resp.headers, parsed, raw


def keys(value):
    return sorted(value.keys()) if isinstance(value, dict) else type(value).__name__


def main(base):
    rows = []
    failures = []

    def record(label, status, note, ok):
        rows.append((label, status, note, "ok" if ok else "DIFFERS"))
        if not ok:
            failures.append(label)

    status, hdrs, body, _ = call(base, "GET", "/status")
    record("GET /status", status, "keys: %s" % keys(body),
           status == 200 and isinstance(body, dict) and "version" in body)
    print("Seerr version: %s" % (body or {}).get("version"), file=sys.stderr)

    status, hdrs, body, _ = call(base, "GET", "/settings/public")
    record("GET /settings/public", status, "keys: %s" % keys(body),
           status == 200 and isinstance(body, dict) and "initialized" in body)
    media_server = (body or {}).get("mediaServerType")
    record("  mediaServerType present", status, "value on a fresh server: %r" % (media_server,), media_server is not None)

    for path in ("/auth/me", "/discover/trending", "/search?query=test", "/request", "/movie/603"):
        status, hdrs, body, _ = call(base, "GET", path)
        record("GET %s without a session" % path, status, "body: %s" % (json.dumps(body)[:80] if body else ""),
               status in (401, 403))

    status, hdrs, body, _ = call(base, "GET", "/auth/me", headers={"X-Api-Key": "not-a-key"})
    record("GET /auth/me with a wrong X-Api-Key", status, "", status in (401, 403))

    status, hdrs, body, _ = call(base, "POST", "/auth/jellyfin",
                                 {"username": "nobody", "password": "wrong"})
    record("POST /auth/jellyfin, wrong user (server not set up)", status,
           "Set-Cookie: %s" % ("yes" if hdrs.get("Set-Cookie") else "no"),
           status in (400, 401, 403, 500) and not hdrs.get("Set-Cookie"))

    status, hdrs, body, _ = call(base, "POST", "/auth/logout")
    record("POST /auth/logout without a session", status, "", status in (200, 401, 403))

    print("| Request | Status | Note | Check |")
    print("| --- | --- | --- | --- |")
    for label, status, note, check in rows:
        print("| %s | %s | %s | %s |" % (label, status, note.replace("|", "/"), check))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1].rstrip("/")))
