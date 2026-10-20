#!/usr/bin/env bash
# Removes the test server container (the image stays for the next run).
set -euo pipefail
docker rm -f "${CONTAINER:-jfd-syncplay-test}" >/dev/null 2>&1 || true
