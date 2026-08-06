# syntax=docker/dockerfile:1
# Benchmark contestant runtime.
# Build context = repo root (see build.sh; it stages host artifacts into
# docker/.context first).

FROM ubuntu:24.04

ARG UID=1004
ARG GID=1004

ENV DEBIAN_FRONTEND=noninteractive \
    HOME=/home/harry \
    PATH=/opt/openmpi/bin:/home/harry/.bun/bin:/home/harry/.local/bin:/opt/codex-standalone/bin:/opt/codex-standalone:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    LD_LIBRARY_PATH=/opt/openmpi/lib

# --- base toolchain ---------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc-13 g++-13 gfortran-13 cmake make ninja-build \
    git curl wget ca-certificates pkg-config file unzip zip zsh \
    python3 python3-pip python3-venv python3-dev \
    nodejs npm ripgrep jq \
    libssl-dev libcurl4-openssl-dev zlib1g-dev libgomp1 libstdc++-13-dev \
    libevent-2.1-7t64 libhwloc15 libnl-3-200 libnl-route-3-200 \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -g ${GID} harry \
    && useradd -m -u ${UID} -g ${GID} -s /bin/zsh harry \
    && mkdir -p /home/harry/.local/share/opencode /home/harry/.local/bin \
    && chown -R harry:harry /home/harry

# --- OpenMPI 5.0.9 (staged from host install tree) --------------------------
COPY docker/.context/openmpi /opt/openmpi

# --- runtimes: bun 1.3.14, uv ----------------------------------------------
RUN curl -fsSL https://bun.sh/install | bash -s "bun-v1.3.14"
RUN curl -LsSf https://astral.sh/uv/install.sh \
    | env UV_INSTALL_DIR=/home/harry/.local/bin sh

# --- harnesses (exact host binaries, staged) --------------------------------
COPY docker/.context/opencode /usr/local/bin/opencode
COPY docker/.context/codex-release /opt/codex-standalone
COPY docker/.context/codegraph /opt/codegraph
RUN chmod +x /usr/local/bin/opencode \
    && ln -sf /opt/codex-standalone/bin/codex /usr/local/bin/codex \
    && ln -sf /opt/codegraph/bin/codegraph /usr/local/bin/codegraph

# --- opencodex proxy + ocx-relay + goal plugin (repo submodules) ------------
COPY opencodex /opt/opencodex
RUN cd /opt/opencodex \
    && if [ -f package.json ]; then \
         /home/harry/.bun/bin/bun install --frozen-lockfile \
         && ln -sf /opt/opencodex/bin/ocx.mjs /usr/local/bin/ocx; \
       else echo "opencodex submodule missing; skipping"; fi
COPY ocx-relay /opt/ocx-relay
COPY OpenCode-goal-plugin /opt/OpenCode-goal-plugin
RUN cd /opt/OpenCode-goal-plugin \
    && if [ -f package.json ]; then npm install --omit=dev --no-audit --no-fund; \
       else echo "goal plugin submodule missing; skipping"; fi

# --- real user configs, mirrored from the host by build.sh ----------------
# Source is docker/.context/configs (gitignored; real keys). The committed
# docker/configs/ tree stays a redacted reference snapshot.
COPY docker/.context/configs/opencode/ /home/harry/.config/opencode/
RUN cd /home/harry/.config/opencode \
    && if [ -f package.json ]; then npm ci --omit=dev --no-audit --no-fund; \
       else echo "no opencode config staged; skipping npm ci"; fi
COPY docker/.context/opencode-goal-plugin /home/harry/.local/share/opencode-goal-plugin
COPY docker/.context/configs/opencode-data/ /home/harry/.local/share/opencode/

COPY docker/.context/configs/codex/ /home/harry/.codex/
COPY docker/.context/configs/opencodex/ /home/harry/.opencodex/

# --- shared DNDSR externals (staged) ----------------------------------------
COPY docker/.context/external /opt/external

# OpenMPI runtime needs libevent_core/libevent_pthreads (separate packages).
# Kept as its own layer so toolchain iterations stay incremental.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libevent-core-2.1-7t64 libevent-pthreads-2.1-7t64 \
    && rm -rf /var/lib/apt/lists/*

# OpenMPI wrapper scripts (mpicc/mpirun) embed the original install prefix and
# resolve share files/libs relative to it; mirror the tree there.
RUN mkdir -p /mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9 \
    && cp -a /opt/openmpi /mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install

COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh \
    && chown -R harry:harry /home/harry

USER harry
WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/bin/zsh"]
