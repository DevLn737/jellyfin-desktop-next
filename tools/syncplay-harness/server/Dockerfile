# syntax=docker/dockerfile:1.7
# Reproducible SyncPlay test server: the playback-rate forks of Jellyfin Server
# and Jellyfin Web (12.2) plus a generated test clip with a machine-readable
# frame barcode. Not a production image: no GPU runtimes, debug-friendly.
#
# Prebuilt outputs can replace the build stages without editing this file:
#   docker buildx build --build-context server-dist=../out/server \
#                       --build-context web-dist=../out/web ...

ARG SERVER_REPO=https://github.com/DevLn737/jellyfin.git
ARG SERVER_REF=494362e6b354385a00b7ae879814e2908c23f725
ARG WEB_REPO=https://github.com/DevLn737/jellyfin-web.git
ARG WEB_REF=b2a97d40c6898726b030b0233a0c9dc89734d3e3

FROM mcr.microsoft.com/dotnet/sdk:10.0 AS server-build
ARG SERVER_REPO
ARG SERVER_REF
WORKDIR /src
RUN git init -q . && git fetch -q --depth 1 "${SERVER_REPO}" "${SERVER_REF}" && git checkout -q FETCH_HEAD
RUN dotnet publish Jellyfin.Server --configuration Release --self-contained \
      --runtime linux-x64 --output /dist -p:DebugSymbols=false -p:DebugType=none

FROM scratch AS server-dist
COPY --from=server-build /dist /

FROM node:24-bookworm AS web-build
ARG WEB_REPO
ARG WEB_REF
WORKDIR /src
RUN git init -q . && git fetch -q --depth 1 "${WEB_REPO}" "${WEB_REF}" && git checkout -q FETCH_HEAD
RUN npm ci --no-audit && npm run build:production

FROM scratch AS web-dist
COPY --from=web-build /src/dist /

# The official 12.2 image provides the exact runtime dependencies (jellyfin-ffmpeg,
# ICU, fonts); only the server and Web payloads are replaced by the forks.
FROM jellyfin/jellyfin:12.2-amd64.20261005-225228 AS runtime
ARG SERVER_REF
ARG WEB_REF
RUN rm -rf /jellyfin
COPY make-media.sh /usr/local/bin/make-media.sh
RUN FFMPEG=/usr/lib/jellyfin-ffmpeg/ffmpeg sh /usr/local/bin/make-media.sh /media
COPY --from=server-dist / /jellyfin
COPY --from=web-dist / /jellyfin/jellyfin-web
LABEL org.opencontainers.image.title="Jellyfin SyncPlay test server" \
      lab.jellyfin.server.revision="${SERVER_REF}" \
      lab.jellyfin.web.revision="${WEB_REF}"
EXPOSE 8096
ENTRYPOINT ["/jellyfin/jellyfin"]
HEALTHCHECK --interval=5s --timeout=5s --start-period=10s --retries=30 \
  CMD curl -fsS http://localhost:8096/health || exit 1
