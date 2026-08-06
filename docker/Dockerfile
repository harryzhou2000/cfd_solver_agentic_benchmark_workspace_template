# syntax=docker/dockerfile:1
# Benchmark contestant runtime.
#
# Everything in the image is installed FRESH from official sources at
# versions pinned to this machine (see the comments); nothing is staged or
# copied from the host. The only host-derived inputs are:
#   - the proxy env vars, forwarded by build.sh as build args when needed;
#   - the user configs, injected at container start by scripts/start.sh
#     (never baked into the image).

FROM ubuntu:24.04

# Proxy build args (predefined Docker args): passed by build.sh only when the
# proxy is on the host loopback (or forced with BUILD_PROXY=1). Declared so
# RUN steps (curl/git/apt) inherit them.
ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG NO_PROXY
ARG http_proxy
ARG https_proxy
ARG no_proxy

ARG JOBS=4

# --- pinned versions / commits (override with --build-arg to switch) --------
ARG OPENCODE_VERSION=1.18.14
ARG CODEX_VERSION=0.146.0
ARG OPENCODEX_REPO=https://github.com/harryzhou2000/opencodex.git
ARG OPENCODEX_COMMIT=ea7f5a69748c666f739143142627f8ace1fcb271
ARG OCX_RELAY_REPO=https://github.com/harryzhou2000/ocx-relay.git
ARG OCX_RELAY_COMMIT=886298b384738b4dc7f5c8fe03ad63ea1beba567
ARG EXTERNAL_HEADERONLYS_REPO=https://github.com/harryzhou2000/cfd_externals_headeronlys
ARG EXTERNAL_HEADERONLYS_TAG=v0.1.0
ARG CFD_EXTERNALS_REPO=https://github.com/harryzhou2000/cfd_externals.git
ARG CFD_EXTERNALS_COMMIT=942cf6700c2089669b66181b66de04a3ddaf7d54

# The image is user-agnostic: it ships one generic user (cfd_agent, uid/gid
# 1000). At container start the entrypoint remaps that user to the invoking
# host user's uid/gid (enroot-style), so mounted host dirs are owned by the
# process. Nothing about the build user is baked in.
ENV DEBIAN_FRONTEND=noninteractive \
    HOME=/home/cfd_agent \
    PATH=/opt/node/bin:/home/cfd_agent/.bun/bin:/home/cfd_agent/.local/bin:/home/cfd_agent/.opencode/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# --- base toolchain (apt; Ubuntu 24.04 defaults = GCC 13, like the host) ----
# Cache mount: downloaded .debs persist across rebuilds in the BuildKit cache
# store, so a changed package list re-downloads only the deltas.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc-13 g++-13 gfortran-13 cmake make ninja-build \
    git curl wget ca-certificates pkg-config file unzip zip zsh \
    python3 python3-pip python3-venv python3-dev \
    ripgrep jq bubblewrap \
    libssl-dev libcurl4-openssl-dev zlib1g-dev libgomp1 libstdc++-13-dev \
    libblas-dev liblapack-dev \
    openmpi-bin libopenmpi-dev \
    && rm -rf /var/lib/apt/lists/*

# Kept byte-identical to the pre-system-install image so the following
# layers (node/doxygen/externals build) stay cached. The dirs themselves are
# leftovers from the old per-user layout and are removed in the final layer.
RUN mkdir -p /home/cfd_agent/.local/bin /home/cfd_agent/.opencode/bin /home/cfd_agent/.bun

# --- Node.js 24.18.0 (host version) — official nodejs.org tarball ----------
RUN curl -fsSL -o /tmp/node.tar.xz \
        https://nodejs.org/dist/v24.18.0/node-v24.18.0-linux-x64.tar.xz \
    && mkdir -p /opt/node \
    && tar -xJf /tmp/node.tar.xz -C /opt/node --strip-components=1 \
    && ln -sf /opt/node/bin/node /usr/local/bin/node \
    && ln -sf /opt/node/bin/npm /usr/local/bin/npm \
    && ln -sf /opt/node/bin/npx /usr/local/bin/npx \
    && rm -f /tmp/node.tar.xz

# --- doxygen (just before the externals build) -------------------------------
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends doxygen \
    && rm -rf /var/lib/apt/lists/*

# --- shared DNDSR externals, built from source (DNDSR recipe) ---------------
# OpenMPI comes from apt (openmpi-bin + libopenmpi-dev, incl. mpifort), so
# mpicc/mpicxx are already on PATH. Installed BEFORE the harnesses (after the
# build stack and uv): the heavy cfd_externals source build should sit on a
# stable layer below the harness installs.
#
# Header-only libraries (Eigen, Boost, CGAL, fmt, pybind11, ...): official
# release tarball of the DNDSR externals bundle, pinned tag
# (EXTERNAL_HEADERONLYS_TAG, default v0.1.0).
RUN mkdir -p /opt/external \
    && curl -fsSL -o /tmp/external_headeronlys.tar.gz \
        "${EXTERNAL_HEADERONLYS_REPO}/releases/download/${EXTERNAL_HEADERONLYS_TAG}/external_headeronlys.tar.gz" \
    && tar -xzf /tmp/external_headeronlys.tar.gz -C /opt/external \
    && rm -f /tmp/external_headeronlys.tar.gz

# cfd_externals (zlib/HDF5/CGNS/ParMETIS/Cantera): source at the pinned
# commit, submodules at their pinned revisions.
RUN git init -q /opt/external/cfd_externals \
    && cd /opt/external/cfd_externals \
    && git remote add origin "${CFD_EXTERNALS_REPO}" \
    && git fetch -q --depth 1 origin "${CFD_EXTERNALS_COMMIT}" \
    && git checkout -q FETCH_HEAD \
    && git submodule update --init --recursive --depth 1

# Build the binary externals from source (zlib, hdf5, cgns, parmetis_fix,
# cantera) exactly like ~/projects/DNDSR does: pip requirements first, then
# CC=mpicc CXX=mpicxx python cfd_externals_build.py. Build trees and source
# clones are removed afterwards; the installed tree stays at
# /opt/external/cfd_externals/install.
# Cache mount: pip downloads persist across rebuilds.
# The venv's bin/ goes on PATH for this step: cfd_externals_build.py invokes
# bare `scons`/`patchelf` (via shutil.which / run_or_die), which must resolve
# to the venv installs — the system python has no scons.
RUN --mount=type=cache,target=/root/.cache/pip \
    python3 -m venv /opt/cfd-venv \
    && /opt/cfd-venv/bin/pip install \
        -r /opt/external/cfd_externals/requirements.txt \
    && cd /opt/external/cfd_externals \
    && CC=mpicc CXX=mpicxx PATH="/opt/cfd-venv/bin:$PATH" JOBS="${JOBS}" \
        /opt/cfd-venv/bin/python cfd_externals_build.py \
    && rm -rf build_* repos

# --- user-space tools: SYSTEM installs only, /opt stores, empty $HOME --------
# Everything below is installed into system prefixes (/usr/local/bin, /opt)
# and opencode's runtime stores are pinned to /opt via XDG_*. The image home
# therefore stays (almost) empty, so the start-time uid/gid remap (see
# entrypoint.sh) never needs to chown a large tree — `usermod -u` and a
# recursive home chown would copy up ~1.3G on overlayfs at EVERY start.
ENV XDG_CONFIG_HOME=/opt/opencode-config \
    XDG_CACHE_HOME=/opt/opencode-cache \
    PATH=/opt/node/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# --- bun 1.3.14 (host version) — official installer, pinned, SYSTEM install --
# The installer honors $BUN_INSTALL: install to /opt/bun so the runtime stays
# out of the image home.
RUN mkdir -p /opt/bun \
    && curl -fsSL https://bun.sh/install | BUN_INSTALL=/opt/bun bash -s "bun-v1.3.14" \
    && ln -sf /opt/bun/bin/bun /usr/local/bin/bun

# --- uv 0.12.1 (host version) — official release tarball --------------------
RUN curl -fsSL -o /tmp/uv.tar.gz \
        https://github.com/astral-sh/uv/releases/download/0.12.1/uv-x86_64-unknown-linux-gnu.tar.gz \
    && tar -xzf /tmp/uv.tar.gz -C /usr/local/bin --strip-components=1 \
    && rm -f /tmp/uv.tar.gz

# --- opencode (host version) — official installer, pinned, SYSTEM install ----
# The official installer hardcodes INSTALL_DIR=$HOME/.opencode/bin, so run it
# with a throwaway HOME and move the standalone binary to /usr/local/bin.
# (npm's opencode-ai package works, but npm 11 blocks postinstall scripts by
# default and leaves a lazy first-run download wrapper; the installer's
# standalone binary is deterministic.)
RUN mkdir -p /opt/opencode-install-home \
    && HOME=/opt/opencode-install-home \
       curl -fsSL https://opencode.ai/install \
       | HOME=/opt/opencode-install-home bash -s -- --version "${OPENCODE_VERSION}" --no-modify-path \
    && mv /opt/opencode-install-home/.opencode/bin/opencode /usr/local/bin/opencode \
    && chmod 755 /usr/local/bin/opencode \
    && rm -rf /opt/opencode-install-home

# --- opencode plugins — official `opencode plugin -g`, pinned ---------------
# Manifest docker/opencode-plugins.json: opencode-goal-plugin at the fork's
# PR-tip commit, oh-my-opencode-slim@2.2.10. Installs from npm/GitHub only,
# never from the host; also writes the image's pristine global opencode
# config (~/.config/opencode/opencode.jsonc) with the plugin array.
COPY docker/opencode-plugins.json /opt/opencode-plugins.json
COPY docker/scripts/install-opencode-plugins.sh /usr/local/bin/install-opencode-plugins
# The plugin store + pristine global config land under /opt (XDG_CACHE_HOME /
# XDG_CONFIG_HOME). chmod happens in THIS layer (no overlay copy-up): the
# remapped container user (arbitrary uid) must be able to refresh models.json,
# fetch provider binaries into .cache/opencode/bin and install plugins at
# runtime.
RUN chmod +x /usr/local/bin/install-opencode-plugins \
    && install-opencode-plugins --manifest /opt/opencode-plugins.json \
    && chmod -R a+rwX /opt/opencode-config /opt/opencode-cache

# --- codex-cli (host version) — official standalone release -----------------
# The standalone tarball extracts a single binary at the top level (no
# bin/ subdirectory).
RUN curl -fsSL -o /tmp/codex.tar.gz \
        "https://github.com/openai/codex/releases/download/rust-v${CODEX_VERSION}/codex-x86_64-unknown-linux-musl.tar.gz" \
    && mkdir -p /opt/codex-standalone \
    && tar -xzf /tmp/codex.tar.gz -C /opt/codex-standalone \
    && ln -sf /opt/codex-standalone/codex-x86_64-unknown-linux-musl /usr/local/bin/codex \
    && rm -f /tmp/codex.tar.gz

# --- codegraph 1.2.0 (host version) — official npm package ------------------
# npm global installs land in /opt/node (system prefix). The npm cache is
# redirected to the cache mount so ~/.npm never appears in the image home.
RUN --mount=type=cache,target=/root/.npm \
    npm_config_cache=/root/.npm \
    npm install -g @colbymchenry/codegraph@1.2.0 --no-audit --no-fund

# --- opencodex proxy — official source install at the pinned commit ---------
RUN --mount=type=cache,target=/root/.bun-cache \
    git init -q /opt/opencodex \
    && cd /opt/opencodex \
    && git remote add origin "${OPENCODEX_REPO}" \
    && git fetch -q --depth 1 origin "${OPENCODEX_COMMIT}" \
    && git checkout -q FETCH_HEAD \
    && BUN_INSTALL_CACHE_DIR=/root/.bun-cache bun install --frozen-lockfile \
    && ln -sf /opt/opencodex/bin/ocx.mjs /usr/local/bin/ocx

# --- ocx-relay — pinned commit (plain node scripts, no build) ---------------
RUN git init -q /opt/ocx-relay \
    && cd /opt/ocx-relay \
    && git remote add origin "${OCX_RELAY_REPO}" \
    && git fetch -q --depth 1 origin "${OCX_RELAY_COMMIT}" \
    && git checkout -q FETCH_HEAD

# --- container entrypoint (opencodex autostart probe) -----------------------
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh \
    # ubuntu:24.04 base ships a stock user/group at uid/gid 1000 ("ubuntu");
    # drop it first so the generic cfd_agent user cleanly owns 1000:1000.
    && U0="$(getent passwd 1000 | cut -d: -f1)"; [ -z "$U0" ] || userdel -r "$U0" \
    && G0="$(getent group 1000 | cut -d: -f1)"; [ -z "$G0" ] || { groupdel "$G0" || true; } \
    && groupadd -g 1000 cfd_agent \
    && useradd -m -u 1000 -g 1000 -s /bin/zsh cfd_agent \
    # Keep the image home minimal: every tool is installed system-wide and
    # the opencode stores live under /opt (XDG_*), so the start-time uid
    # remap never needs a recursive home chown. Drop any leftover per-user
    # state here (same layer, no overlay copy-up).
    && find /home/cfd_agent -mindepth 1 -delete \
    && chown cfd_agent:cfd_agent /home/cfd_agent

# Runs as root so the entrypoint can remap cfd_agent to the host uid/gid; it
# immediately drops privileges via setpriv before exec'ing the command.
USER root
WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/bin/zsh"]
