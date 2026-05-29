# paper-runtime/Dockerfile
# owner: 老吴 (linux-sre-devops, A-unit, #10)
# last_review: 2026-05-29
# target: eu-central-1 Frankfurt — Ubuntu 24.04 + cmake + cpp-httplib + websocat
# build: docker build -t stcpp-paper:latest .
# run:  docker run --env-file /etc/stcpp/paper.env stcpp-paper:latest
#
# Stage 1 — build (Ubuntu 24.04, gcc-13)
# Stage 2 — runtime (Ubuntu 24.04 minimal, no build tools)
#
# ADR-011 cite: paper binary 与 live binary 共享同一 CMake target (PAPER_MODE=ON),
#               同一镜像, 切 live 只换 .env PAPER_MODE=0 + restart.
# ADR-013 v2 cite: deployment target eu-central-1 Frankfurt (Draft → Accepted 待 W10 W2 实测 ack).
# CLAUDE.md red-line: paper mode 不写 position/pnl_ledger/nonce_ledger (R-11).

# ---------------------------------------------------------------------------
# Stage 1: builder
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_TYPE=Release

# Install build toolchain
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-13 \
    g++-13 \
    cmake \
    ninja-build \
    git \
    libssl-dev \
    zlib1g-dev \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Set gcc-13 as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
 && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

WORKDIR /src

# Copy source (respects .dockerignore)
COPY . .

# CMake configure + build
# PAPER_MODE=ON: links paper_engine, skips live order submission, skips nonce_ledger writes
RUN cmake -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DPAPER_MODE=ON \
      -DCMAKE_C_COMPILER=gcc-13 \
      -DCMAKE_CXX_COMPILER=g++-13 \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure -j4

# ---------------------------------------------------------------------------
# Stage 2: runtime
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

# Minimal runtime libs only
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    libssl3 \
    ca-certificates \
    curl \
    netcat-openbsd \
    websocat \
    tzdata \
    && rm -rf /var/lib/apt/lists/*

# NTP/timezone: Europe/Berlin (Frankfurt UTC+1/+2)
ENV TZ=Europe/Berlin
RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone

# Non-root user
RUN useradd -r -s /bin/false -d /app stcpp

# Binary + config dirs
RUN mkdir -p /app/bin /app/data /app/logs /app/config \
 && chown -R stcpp:stcpp /app

# Copy built binary from builder stage
COPY --from=builder --chown=stcpp:stcpp /src/build/src/stcpp/paper_runtime /app/bin/paper_runtime

# Healthcheck: probe local debug REST endpoint (老周 laozhou-w8-debug-rest-api-spec-v1.md)
# port 9090 = debug REST (not trading port)
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl -sf http://localhost:9090/health || exit 1

WORKDIR /app

USER stcpp

# Env defaults (overridden by --env-file /etc/stcpp/paper.env at runtime)
ENV PAPER_MODE=1
ENV LOG_DIR=/app/logs
ENV DATA_DIR=/app/data

ENTRYPOINT ["/app/bin/paper_runtime"]
CMD ["--config", "/app/config/paper.toml"]
