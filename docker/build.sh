#!/usr/bin/env bash
# Build the benchmark contestant runtime image.
#
# The image is self-contained: every tool is installed fresh from its
# official source at a version pinned to this machine (see docker/Dockerfile
# and docker/opencode-plugins.json). Nothing is staged or copied from the
# host — there is no docker/.context staging anymore. The only host-derived
# inputs are the proxy env vars, forwarded below when needed.
#
# Proxy: only already-exported env vars are used (no proxy script is
# sourced). Proxy build-args plus --network host are passed only when the
# proxy is on the host loopback (build containers cannot reach the host's
# 127.0.0.1 otherwise). LAN proxies are skipped by default — direct
# connectivity avoids flaky apt/npm failures through the proxy; force them
# with BUILD_PROXY=1.
#
# Note: the docker build does NOT inherit the shell environment, so export
# HTTP_PROXY/HTTPS_PROXY/NO_PROXY (e.g. by sourcing ~/.setproxy.sh) before
# running this script if the build needs a proxy.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

IMAGE="${IMAGE:-cfd-bench:latest}"
JOBS="${JOBS:-4}"

BUILD_PROXY="${BUILD_PROXY:-0}"
case "${HTTP_PROXY:-}${http_proxy:-}" in
  *127.0.0.1*|*localhost*) BUILD_PROXY=1 ;;
esac
BUILD_OPTS=()
if [ "$BUILD_PROXY" = "1" ]; then
  BUILD_OPTS+=(--network host)
  for v in HTTP_PROXY HTTPS_PROXY NO_PROXY http_proxy https_proxy no_proxy; do
    if [ -n "${!v:-}" ]; then BUILD_OPTS+=(--build-arg "$v=${!v}"); fi
  done
fi
if [ "${BUILD_NO_CACHE:-0}" = "1" ]; then
  BUILD_OPTS+=(--no-cache)
fi
if [ -n "${BUILD_PROGRESS:-}" ]; then
  BUILD_OPTS+=(--progress "$BUILD_PROGRESS")
fi
# Forward version/commit overrides (defaults live in the Dockerfile ARGs).
for v in OPENCODE_VERSION CODEX_VERSION \
         OPENCODEX_REPO OPENCODEX_COMMIT \
         OCX_RELAY_REPO OCX_RELAY_COMMIT \
         EXTERNAL_HEADERONLYS_REPO EXTERNAL_HEADERONLYS_TAG \
         CFD_EXTERNALS_REPO CFD_EXTERNALS_COMMIT; do
  if [ -n "${!v:-}" ]; then BUILD_OPTS+=(--build-arg "$v=${!v}"); fi
done

echo "== docker build =="
docker build "${BUILD_OPTS[@]}" --build-arg "JOBS=${JOBS}" \
  -f docker/Dockerfile -t "$IMAGE" .
echo "built $IMAGE"
