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

ARG UID=1004
ARG GID=1004
ARG JOBS=4

ENV DEBIAN_FRONTEND=noninteractive \
    HOME=/home/harry \
    PATH=/opt/node/bin:/home/harry/.bun/bin:/home/harry/.local/bin:/home/harry/.opencode/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

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

RUN groupadd -g ${GID} harry \
    && useradd -m -u ${UID} -g ${GID} -s /bin/zsh harry \
    && mkdir -p /home/harry/.local/bin /home/harry/.opencode/bin /home/harry/.bun \
    && chown -R harry:harry /home/harry

# --- Node.js 24.18.0 (host version) — official nodejs.org tarball ----------
RUN curl -fsSL -o /tmp/node.tar.xz \
        https://nodejs.org/dist/v24.18.0/node-v24.18.0-linux-x64.tar.xz \
    && mkdir -p /opt/node \
    && tar -xJf /tmp/node.tar.xz -C /opt/node --strip-components=1 \
    && ln -sf /opt/node/bin/node /usr/local/bin/node \
    && ln -sf /opt/node/bin/npm /usr/local/bin/npm \
    && ln -sf /opt/node/bin/npx /usr/local/bin/npx \
    && rm -f /tmp/node.tar.xz

# --- bun 1.3.14 (host version) — official installer, version-pinned ---------
RUN curl -fsSL https://bun.sh/install | bash -s "bun-v1.3.14"

# --- uv 0.12.1 (host version) — official release tarball --------------------
RUN curl -fsSL -o /tmp/uv.tar.gz \
        https://github.com/astral-sh/uv/releases/download/0.12.1/uv-x86_64-unknown-linux-gnu.tar.gz \
    && tar -xzf /tmp/uv.tar.gz -C /home/harry/.local/bin --strip-components=1 \
    && chmod +x /home/harry/.local/bin/uv /home/harry/.local/bin/uvx \
    && rm -f /tmp/uv.tar.gz

# --- shared DNDSR externals, built from source (DNDSR recipe) ---------------
# OpenMPI comes from apt (openmpi-bin + libopenmpi-dev, incl. mpifort), so
# mpicc/mpicxx are already on PATH. Installed BEFORE the harnesses (after the
# build stack and uv): the heavy cfd_externals source build should sit on a
# stable layer below the harness installs.
#
# Header-only libraries (Eigen, Boost, CGAL, fmt, pybind11, ...): official
# release tarball of the DNDSR externals bundle, pinned tag v0.1.0.
RUN mkdir -p /opt/external \
    && curl -fsSL -o /tmp/external_headeronlys.tar.gz \
        https://github.com/harryzhou2000/cfd_externals_headeronlys/releases/download/v0.1.0/external_headeronlys.tar.gz \
    && tar -xzf /tmp/external_headeronlys.tar.gz -C /opt/external \
    && rm -f /tmp/external_headeronlys.tar.gz

# cfd_externals (zlib/HDF5/CGNS/ParMETIS/Cantera): source at the pinned
# commit, submodules at their pinned revisions.
RUN git init -q /opt/external/cfd_externals \
    && cd /opt/external/cfd_externals \
    && git remote add origin https://github.com/harryzhou2000/cfd_externals.git \
    && git fetch -q --depth 1 origin 942cf6700c2089669b66181b66de04a3ddaf7d54 \
    && git checkout -q FETCH_HEAD \
    && git submodule update --init --recursive --depth 1

# Build the binary externals from source (zlib, hdf5, cgns, parmetis_fix,
# cantera) exactly like ~/projects/DNDSR does: pip requirements first, then
# CC=mpicc CXX=mpicxx python cfd_externals_build.py. Build trees and source
# clones are removed afterwards; the installed tree stays at
# /opt/external/cfd_externals/install.
# Cache mount: pip downloads persist across rebuilds.
RUN --mount=type=cache,target=/root/.cache/pip \
    python3 -m venv /opt/cfd-venv \
    && /opt/cfd-venv/bin/pip install -q \
        -r /opt/external/cfd_externals/requirements.txt \
    && cd /opt/external/cfd_externals \
    && CC=mpicc CXX=mpicxx JOBS="${JOBS}" \
        /opt/cfd-venv/bin/python cfd_externals_build.py \
    && rm -rf build_* repos \
    && chown -R harry:harry /opt/external

# --- opencode 1.18.11 (host version) — official installer, version-pinned ---
RUN curl -fsSL https://opencode.ai/install | bash -s -- --version 1.18.11 --no-modify-path

# --- opencode plugins — official `opencode plugin -g`, pinned ---------------
# Manifest docker/opencode-plugins.json: opencode-goal-plugin at the fork's
# PR-tip commit, oh-my-opencode-slim@2.2.10. Installs from npm/GitHub only,
# never from the host; also writes the image's pristine global opencode
# config (~/.config/opencode/opencode.jsonc) with the plugin array.
COPY docker/opencode-plugins.json /opt/opencode-plugins.json
COPY docker/scripts/install-opencode-plugins.sh /usr/local/bin/install-opencode-plugins
# Cache mount: opencode's plugin installer uses bun — its package cache
# persists across rebuilds.
RUN --mount=type=cache,target=/home/harry/.bun/install/cache \
    chmod +x /usr/local/bin/install-opencode-plugins \
    && install-opencode-plugins --manifest /opt/opencode-plugins.json

# --- codex-cli 0.146.0 (host version) — official standalone release ---------
RUN curl -fsSL -o /tmp/codex.tar.gz \
        https://github.com/openai/codex/releases/download/rust-v0.146.0/codex-x86_64-unknown-linux-musl.tar.gz \
    && mkdir -p /opt/codex-standalone \
    && tar -xzf /tmp/codex.tar.gz -C /opt/codex-standalone \
    && ln -sf /opt/codex-standalone/codex-x86_64-unknown-linux-musl/bin/codex /usr/local/bin/codex \
    && rm -f /tmp/codex.tar.gz

# --- codegraph 1.2.0 (host version) — official npm package ------------------
# Cache mount: npm downloads persist across rebuilds.
RUN --mount=type=cache,target=/root/.npm \
    npm install -g @colbymchenry/codegraph@1.2.0 --no-audit --no-fund

# --- opencodex proxy — official source install at the pinned commit ---------
# Cache mount: bun's package cache persists across rebuilds.
RUN --mount=type=cache,target=/home/harry/.bun/install/cache \
    git init -q /opt/opencodex \
    && cd /opt/opencodex \
    && git remote add origin https://github.com/harryzhou2000/opencodex.git \
    && git fetch -q --depth 1 origin ea7f5a69748c666f739143142627f8ace1fcb271 \
    && git checkout -q FETCH_HEAD \
    && /home/harry/.bun/bin/bun install --frozen-lockfile \
    && ln -sf /opt/opencodex/bin/ocx.mjs /usr/local/bin/ocx

# --- ocx-relay — pinned commit (plain node scripts, no build) ---------------
RUN git init -q /opt/ocx-relay \
    && cd /opt/ocx-relay \
    && git remote add origin https://github.com/harryzhou2000/ocx-relay.git \
    && git fetch -q --depth 1 origin 886298b384738b4dc7f5c8fe03ad63ea1beba567 \
    && git checkout -q FETCH_HEAD

# --- container entrypoint (opencodex autostart probe) -----------------------
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh \
    && chown -R harry:harry /home/harry

USER harry
WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/bin/zsh"]
