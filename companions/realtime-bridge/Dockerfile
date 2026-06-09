# ─────────────────────────────────────────────────────────────────────────────
#  realtime-bridge — bidirectional IRC ↔ realtime-agnostic bridge (Rust).
#  Multi-stage cargo --locked build to a slim runtime; pure-Rust TLS (rustls).
# ─────────────────────────────────────────────────────────────────────────────

FROM rust:1-slim AS build
WORKDIR /app
COPY Cargo.toml Cargo.lock ./
COPY src ./src
RUN cargo build --release --locked

FROM debian:bookworm-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd -r -u 10003 bridge
COPY --from=build /app/target/release/realtime-bridge /usr/local/bin/realtime-bridge
USER bridge
ENTRYPOINT ["realtime-bridge"]
