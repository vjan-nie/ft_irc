//! realtime-bridge — a bidirectional bridge between `ircserv` and
//! `realtime-agnostic` (a Rust WebSocket pub/sub fan-out engine).
//!
//!   IRC → realtime : every PRIVMSG in a bridged channel is POSTed to
//!     realtime-agnostic's `/v1/publish` under topic `irc:<channel>:chat/message`,
//!     so browser / WebSocket clients (and DB-CDC consumers) see live IRC traffic.
//!
//!   realtime → IRC : the bridge opens a WebSocket to realtime-agnostic,
//!     subscribes to the configured inbound topics (DB change-capture, browser
//!     events, …) and injects each event into the matching IRC channel as a
//!     PRIVMSG.
//!
//! Loop-free by topic-namespace separation: IRC-sourced events live under the
//! `irc:**` namespace, which the bridge never subscribes to. Browser/producer
//! messages destined for IRC are published under `irc-in/<channel>`.
//!
//! Configuration via environment variables:
//!   IRC_HOST            (default 127.0.0.1)
//!   IRC_PORT            (default 6667)
//!   IRC_PASS            (server password)
//!   IRC_NICK            (default "rtbridge")
//!   IRC_CHANNELS        (comma-separated, e.g. "#general,#ops")
//!   RT_PUBLISH_URL      (default http://realtime:4000/v1/publish)
//!   RT_WS_URL           (default ws://realtime:4000/ws)
//!   RT_INBOUND_TOPICS   (default "pg/**,mongo/**,irc-in/**")
//!   RT_DEFAULT_CHANNEL  (default "#general")

use std::env;
use std::sync::Arc;

use futures_util::{SinkExt, StreamExt};
use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::TcpStream;
use tokio::sync::mpsc;
use tokio_tungstenite::tungstenite::Message as Ws;

const IRC_MAX_MSG: usize = 400;

struct Config {
    irc_host: String,
    irc_port: u16,
    irc_pass: String,
    irc_nick: String,
    channels: Vec<String>,
    rt_publish_url: String,
    rt_ws_url: String,
    inbound_topics: Vec<String>,
    default_channel: String,
}

fn hashed(name: &str) -> String {
    if name.starts_with('#') {
        name.to_string()
    } else {
        format!("#{name}")
    }
}

impl Config {
    fn from_env() -> Self {
        let channels = env::var("IRC_CHANNELS")
            .unwrap_or_default()
            .split(',')
            .map(str::trim)
            .filter(|s| !s.is_empty())
            .map(hashed)
            .collect();
        let inbound_topics = env::var("RT_INBOUND_TOPICS")
            .unwrap_or_else(|_| "pg/**,mongo/**,irc-in/**".to_string())
            .split(',')
            .map(str::trim)
            .filter(|s| !s.is_empty())
            .map(String::from)
            .collect();
        Config {
            irc_host: env::var("IRC_HOST").unwrap_or_else(|_| "127.0.0.1".to_string()),
            irc_port: env::var("IRC_PORT").ok().and_then(|s| s.parse().ok()).unwrap_or(6667),
            irc_pass: env::var("IRC_PASS").unwrap_or_default(),
            irc_nick: env::var("IRC_NICK").unwrap_or_else(|_| "rtbridge".to_string()),
            channels,
            rt_publish_url: env::var("RT_PUBLISH_URL")
                .unwrap_or_else(|_| "http://realtime:4000/v1/publish".to_string()),
            rt_ws_url: env::var("RT_WS_URL")
                .unwrap_or_else(|_| "ws://realtime:4000/ws".to_string()),
            inbound_topics,
            default_channel: hashed(
                &env::var("RT_DEFAULT_CHANNEL").unwrap_or_else(|_| "#general".to_string()),
            ),
        }
    }
}

#[tokio::main]
async fn main() {
    let cfg = Arc::new(Config::from_env());
    loop {
        if let Err(e) = run(cfg.clone()).await {
            eprintln!("session error: {e}; retrying in 3s");
            tokio::time::sleep(std::time::Duration::from_secs(3)).await;
        }
    }
}

async fn run(cfg: Arc<Config>) -> Result<(), Box<dyn std::error::Error>> {
    // ── IRC connection ──
    let stream = TcpStream::connect((cfg.irc_host.as_str(), cfg.irc_port)).await?;
    let (rd, wr) = stream.into_split();
    let mut irc_reader = BufReader::new(rd);

    // One writer task funnels all outbound IRC lines (so the WS→IRC path and
    // PING/PONG never interleave a half-written line).
    let (irc_tx, mut irc_rx) = mpsc::channel::<String>(256);
    tokio::spawn(async move {
        let mut w = wr;
        while let Some(line) = irc_rx.recv().await {
            if w.write_all(line.as_bytes()).await.is_err() {
                break;
            }
            if w.write_all(b"\r\n").await.is_err() {
                break;
            }
        }
    });

    if !cfg.irc_pass.is_empty() {
        irc_tx.send(format!("PASS {}", cfg.irc_pass)).await?;
    }
    irc_tx.send(format!("NICK {}", cfg.irc_nick)).await?;
    irc_tx
        .send(format!("USER {} 0 * :realtime bridge", cfg.irc_nick))
        .await?;
    for ch in &cfg.channels {
        irc_tx.send(format!("JOIN {ch}")).await?;
    }
    eprintln!(
        "IRC connected to {}:{} as {} (channels: {:?})",
        cfg.irc_host, cfg.irc_port, cfg.irc_nick, cfg.channels
    );

    let http = reqwest::Client::new();

    // ── realtime WebSocket: AUTH + subscribe to inbound topics ──
    let (ws_stream, _) = tokio_tungstenite::connect_async(&cfg.rt_ws_url).await?;
    let (mut ws_sink, mut ws_read) = ws_stream.split();
    ws_sink
        .send(Ws::Text(
            json!({ "type": "AUTH", "token": cfg.irc_nick }).to_string(),
        ))
        .await?;
    let subs: Vec<Value> = cfg
        .inbound_topics
        .iter()
        .enumerate()
        .map(|(i, t)| json!({ "sub_id": format!("in-{i}"), "topic": t }))
        .collect();
    ws_sink
        .send(Ws::Text(
            json!({ "type": "SUBSCRIBE_BATCH", "subscriptions": subs }).to_string(),
        ))
        .await?;
    eprintln!("realtime WS connected; subscribed to {:?}", cfg.inbound_topics);

    // realtime → IRC task
    let irc_tx2 = irc_tx.clone();
    let cfg2 = cfg.clone();
    let ws_to_irc = tokio::spawn(async move {
        while let Some(item) = ws_read.next().await {
            match item {
                Ok(Ws::Text(txt)) => {
                    if let Ok(v) = serde_json::from_str::<Value>(&txt) {
                        if v.get("type").and_then(Value::as_str) == Some("EVENT") {
                            if let Some(ev) = v.get("event") {
                                inject_event(&cfg2, &irc_tx2, ev).await;
                            }
                        }
                    }
                }
                Ok(Ws::Ping(_)) | Ok(Ws::Pong(_)) => {}
                Ok(Ws::Close(_)) | Err(_) => break,
                _ => {}
            }
        }
        eprintln!("realtime WS closed");
    });

    // ── IRC → realtime (main read loop) ──
    let mut line = String::new();
    loop {
        line.clear();
        let n = irc_reader.read_line(&mut line).await?;
        if n == 0 {
            ws_to_irc.abort();
            return Err("IRC connection closed".into());
        }
        let raw = line.trim_end_matches(['\r', '\n']).to_string();
        if raw.is_empty() {
            continue;
        }
        if let Some(tok) = raw.strip_prefix("PING ") {
            irc_tx.send(format!("PONG {tok}")).await?;
            continue;
        }
        if let Some(pm) = parse_privmsg(&raw) {
            if pm.sender.eq_ignore_ascii_case(&cfg.irc_nick) {
                continue; // never re-publish our own injected lines
            }
            if !pm.target.starts_with('#') {
                continue; // channels only
            }
            let chan = pm.target.trim_start_matches('#').to_string();
            let body = json!({
                "topic": format!("irc:{chan}:chat/message"),
                "event_type": "chat.message",
                "payload": { "from": pm.sender, "channel": pm.target, "text": pm.text },
            });
            let url = cfg.rt_publish_url.clone();
            let http2 = http.clone();
            tokio::spawn(async move {
                if let Err(e) = http2.post(&url).json(&body).send().await {
                    eprintln!("publish error: {e}");
                }
            });
        }
    }
}

/// Inject one realtime EVENT into the matching IRC channel.
async fn inject_event(cfg: &Config, irc_tx: &mpsc::Sender<String>, ev: &Value) {
    let topic = ev.get("topic").and_then(Value::as_str).unwrap_or("");
    let etype = ev.get("event_type").and_then(Value::as_str).unwrap_or("event");
    let payload = ev.get("payload");

    // Target channel: `irc-in/<chan>` → #<chan>; else payload.channel; else default.
    let channel = if let Some(rest) = topic.strip_prefix("irc-in/") {
        let seg = rest.split('/').next().unwrap_or("");
        if seg.is_empty() {
            cfg.default_channel.clone()
        } else {
            hashed(seg)
        }
    } else if let Some(c) = payload
        .and_then(|p| p.get("channel"))
        .and_then(Value::as_str)
    {
        hashed(c)
    } else {
        cfg.default_channel.clone()
    };

    let text = render(topic, etype, payload);
    for chunk in chunk_irc(&text) {
        if irc_tx
            .send(format!("PRIVMSG {channel} :{chunk}"))
            .await
            .is_err()
        {
            break;
        }
    }
}

/// Turn an event into a readable one-line IRC message. A chat-shaped payload
/// (`from` + `text`) renders as `<from> text`; anything else as a compact
/// `[event_type] {json}`.
fn render(_topic: &str, etype: &str, payload: Option<&Value>) -> String {
    if let Some(obj) = payload.and_then(Value::as_object) {
        let from = obj
            .get("from")
            .or_else(|| obj.get("user"))
            .and_then(Value::as_str);
        let text = obj
            .get("text")
            .or_else(|| obj.get("message"))
            .and_then(Value::as_str);
        match (from, text) {
            (Some(f), Some(t)) => return format!("<{f}> {t}"),
            (None, Some(t)) => return t.to_string(),
            _ => {}
        }
    }
    let body = payload.map(Value::to_string).unwrap_or_default();
    format!("[{etype}] {body}")
}

fn chunk_irc(s: &str) -> Vec<String> {
    if s.len() <= IRC_MAX_MSG {
        return vec![s.to_string()];
    }
    let mut out = Vec::new();
    let mut buf = String::new();
    for word in s.split(' ') {
        if !buf.is_empty() && buf.len() + 1 + word.len() > IRC_MAX_MSG {
            out.push(std::mem::take(&mut buf));
        }
        if !buf.is_empty() {
            buf.push(' ');
        }
        buf.push_str(word);
    }
    if !buf.is_empty() {
        out.push(buf);
    }
    out
}

struct PrivMsg {
    sender: String,
    target: String,
    text: String,
}

/// Parse `:nick!user@host PRIVMSG <target> :<text>`.
fn parse_privmsg(raw: &str) -> Option<PrivMsg> {
    let rest = raw.strip_prefix(':')?;
    let (prefix, after) = rest.split_once(' ')?;
    let sender = prefix.split('!').next().unwrap_or(prefix).to_string();
    let mut parts = after.splitn(2, ' ');
    if !parts.next()?.eq_ignore_ascii_case("PRIVMSG") {
        return None;
    }
    let (target, trailing) = parts.next()?.split_once(" :")?;
    Some(PrivMsg {
        sender,
        target: target.trim().to_string(),
        text: trailing.to_string(),
    })
}
