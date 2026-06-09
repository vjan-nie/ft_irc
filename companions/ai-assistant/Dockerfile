# ─────────────────────────────────────────────────────────────────────────────
#  ai-assistant — the Claude-backed IRC companion (Rust).
#
#  A separate process that connects to ircserv as an ordinary IRC client and
#  answers when addressed. Multi-stage: cargo build, then a slim runtime.
#  TLS is pure-Rust (rustls), so no system OpenSSL is required.
# ─────────────────────────────────────────────────────────────────────────────

# ---- build ----------------------------------------------------------------
FROM rust:1-slim AS build
WORKDIR /app
COPY Cargo.toml ./
COPY src ./src
RUN cargo build --release

# ---- runtime --------------------------------------------------------------
FROM debian:bookworm-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd -r -u 10002 assistant
COPY --from=build /app/target/release/ai-assistant /usr/local/bin/ai-assistant
USER assistant
# All configuration is via environment variables (see README / docker-compose).
ENTRYPOINT ["ai-assistant"]
