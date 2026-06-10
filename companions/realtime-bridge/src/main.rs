//! realtime-bridge — a bidirectional bridge between `ircserv` and
//! `realtime-agnostic` (a Rust WebSocket pub/sub fan-out engine).
//!
//!   IRC → realtime : every PRIVMSG in a bridged channel is POSTed to
//!     realtime-agnostic's `/v1/publish` under topic `irc:<channel>:chat/message`.
//!
//!   realtime → IRC : the bridge subscribes (WebSocket `/ws`) to the configured
//!     inbound topics and injects each event into the matching IRC channel.
//!     A *chat* event (payload has `from` + `text`) is delivered by a per-web-user
//!     "puppet": a short-lived IRC connection that registers the web user's own
//!     nick, so the message appears in IRC as that user rather than the bridge.
//!     System/CDC events (no `from`) are injected by the main `rtbridge` client.
//!
//! Loop-free by topic-namespace separation: IRC-sourced events live under the
//! `irc:**` namespace, which the bridge never subscribes to. Only the main
//! `rtbridge` connection publishes IRC → realtime; puppets are write-only.
//!
//! Configuration via environment variables:
//!   IRC_HOST IRC_PORT IRC_PASS IRC_NICK IRC_CHANNELS
//!   RT_PUBLISH_URL RT_WS_URL RT_INBOUND_TOPICS RT_DEFAULT_CHANNEL
//!   RT_PUPPETS (true|false, default true)
//!   RT_PUPPET_MAX (default 50)        — cap on concurrent puppet connections
//!   RT_PUPPET_TTL_SECS (default 120)  — idle lifetime before a puppet quits

use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use std::time::Duration;

use futures_util::{SinkExt, StreamExt};
use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::tcp::OwnedWriteHalf;
use tokio::net::TcpStream;
use tokio::sync::{mpsc, Mutex};
use tokio::time::timeout;
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
    puppets: bool,
    puppet_max: usize,
    puppet_ttl: Duration,
}

fn hashed(name: &str) -> String {
    if name.starts_with('#') {
        name.to_string()
    } else {
        format!("#{name}")
    }
}

fn env_or<T: std::str::FromStr>(key: &str, default: T) -> T {
    std::env::var(key).ok().and_then(|s| s.parse().ok()).unwrap_or(default)
}

impl Config {
    fn from_env() -> Self {
        let channels = std::env::var("IRC_CHANNELS")
            .unwrap_or_default()
            .split(',')
            .map(str::trim)
            .filter(|s| !s.is_empty())
            .map(hashed)
            .collect();
        let inbound_topics = std::env::var("RT_INBOUND_TOPICS")
            .unwrap_or_else(|_| "pg/**,mongo/**,irc-in/**".to_string())
            .split(',')
            .map(str::trim)
            .filter(|s| !s.is_empty())
            .map(String::from)
            .collect();
        Config {
            irc_host: std::env::var("IRC_HOST").unwrap_or_else(|_| "127.0.0.1".to_string()),
            irc_port: env_or("IRC_PORT", 6667),
            irc_pass: std::env::var("IRC_PASS").unwrap_or_default(),
            irc_nick: std::env::var("IRC_NICK").unwrap_or_else(|_| "rtbridge".to_string()),
            channels,
            rt_publish_url: std::env::var("RT_PUBLISH_URL")
                .unwrap_or_else(|_| "http://realtime:4000/v1/publish".to_string()),
            rt_ws_url: std::env::var("RT_WS_URL")
                .unwrap_or_else(|_| "ws://realtime:4000/ws".to_string()),
            inbound_topics,
            default_channel: hashed(
                &std::env::var("RT_DEFAULT_CHANNEL").unwrap_or_else(|_| "#general".to_string()),
            ),
            puppets: std::env::var("RT_PUPPETS").map(|v| v != "false" && v != "0").unwrap_or(true),
            puppet_max: env_or("RT_PUPPET_MAX", 50usize),
            puppet_ttl: Duration::from_secs(env_or("RT_PUPPET_TTL_SECS", 120u64)),
        }
    }
}

/// A message for a puppet to deliver as itself.
struct PuppetMsg {
    channel: String,
    text: String,
}

type Hub = Arc<Mutex<HashMap<String, mpsc::Sender<PuppetMsg>>>>;

#[tokio::main]
async fn main() {
    let cfg = Arc::new(Config::from_env());
    loop {
        if let Err(e) = run(cfg.clone()).await {
            eprintln!("session error: {e}; retrying in 3s");
            tokio::time::sleep(Duration::from_secs(3)).await;
        }
    }
}

async fn run(cfg: Arc<Config>) -> Result<(), Box<dyn std::error::Error>> {
    // ── main "rtbridge" IRC connection (IRC→realtime + system-event inject) ──
    let stream = TcpStream::connect((cfg.irc_host.as_str(), cfg.irc_port)).await?;
    let (rd, wr) = stream.into_split();
    let mut irc_reader = BufReader::new(rd);

    let (irc_tx, mut irc_rx) = mpsc::channel::<String>(256);
    tokio::spawn(async move {
        let mut w = wr;
        while let Some(line) = irc_rx.recv().await {
            if send_line(&mut w, &line).await.is_err() {
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
        "IRC connected to {}:{} as {} (channels: {:?}, puppets: {})",
        cfg.irc_host, cfg.irc_port, cfg.irc_nick, cfg.channels, cfg.puppets
    );

    let http = reqwest::Client::new();
    let hub: Hub = Arc::new(Mutex::new(HashMap::new()));

    // ── realtime WebSocket: AUTH + subscribe to inbound topics ──
    let (ws_stream, _) = tokio_tungstenite::connect_async(&cfg.rt_ws_url).await?;
    let (mut ws_sink, mut ws_read) = ws_stream.split();
    ws_sink
        .send(Ws::Text(json!({ "type": "AUTH", "token": cfg.irc_nick }).to_string()))
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
    let hub2 = hub.clone();
    let ws_to_irc = tokio::spawn(async move {
        while let Some(item) = ws_read.next().await {
            match item {
                Ok(Ws::Text(txt)) => {
                    if let Ok(v) = serde_json::from_str::<Value>(&txt) {
                        if v.get("type").and_then(Value::as_str) == Some("EVENT") {
                            if let Some(ev) = v.get("event") {
                                handle_event(&cfg2, &hub2, &irc_tx2, ev).await;
                            }
                        }
                    }
                }
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
            if pm.sender.eq_ignore_ascii_case(&cfg.irc_nick) || !pm.target.starts_with('#') {
                continue;
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

/// Route one realtime EVENT to IRC: chat → a per-user puppet, else → rtbridge.
async fn handle_event(cfg: &Arc<Config>, hub: &Hub, irc_tx: &mpsc::Sender<String>, ev: &Value) {
    let topic = ev.get("topic").and_then(Value::as_str).unwrap_or("");
    let etype = ev.get("event_type").and_then(Value::as_str).unwrap_or("event");
    let payload = ev.get("payload");

    let channel = target_channel(cfg, topic, payload);

    let from = payload
        .and_then(|p| p.get("from").or_else(|| p.get("user")))
        .and_then(Value::as_str);
    let text = payload
        .and_then(|p| p.get("text").or_else(|| p.get("message")))
        .and_then(Value::as_str);

    if let (true, Some(from), Some(text)) = (cfg.puppets, from, text) {
        // Chat message from a web user → deliver as that user via a puppet.
        if route_to_puppet(cfg, hub, from, &channel, text).await {
            return;
        }
        // puppet pool full → fall back to attributed inject via rtbridge
        for chunk in chunk_irc(&format!("<{from}> {text}")) {
            let _ = irc_tx.send(format!("PRIVMSG {channel} :{chunk}")).await;
        }
        return;
    }

    // System / CDC event (no chat shape) → inject via rtbridge.
    let rendered = render(etype, payload);
    for chunk in chunk_irc(&rendered) {
        let _ = irc_tx.send(format!("PRIVMSG {channel} :{chunk}")).await;
    }
}

/// Hand the message to the web user's puppet, spawning one if needed.
/// Returns false when the puppet pool is full (caller should fall back).
async fn route_to_puppet(cfg: &Arc<Config>, hub: &Hub, from: &str, channel: &str, text: &str) -> bool {
    let msg = || PuppetMsg { channel: channel.to_string(), text: text.to_string() };

    // Existing, live puppet?
    {
        let mut map = hub.lock().await;
        if let Some(tx) = map.get(from) {
            if tx.send(msg()).await.is_ok() {
                return true;
            }
            map.remove(from); // stale (task exited) — fall through to recreate
        }
        if map.len() >= cfg.puppet_max {
            return false;
        }
    }

    // Spawn a new puppet for this web user.
    let (tx, rx) = mpsc::channel::<PuppetMsg>(64);
    {
        let mut map = hub.lock().await;
        if map.len() >= cfg.puppet_max {
            return false;
        }
        map.insert(from.to_string(), tx.clone());
    }
    let cfg_c = cfg.clone();
    let hub_c = hub.clone();
    let key = from.to_string();
    let base = derive_nick(from);
    tokio::spawn(async move {
        if let Err(e) = run_puppet(&cfg_c, &base, rx).await {
            eprintln!("puppet {key} ({base}) ended: {e}");
        }
        hub_c.lock().await.remove(&key);
    });
    let _ = tx.send(msg()).await;
    true
}

/// One short-lived IRC connection acting as a single web user.
async fn run_puppet(
    cfg: &Config,
    base_nick: &str,
    mut rx: mpsc::Receiver<PuppetMsg>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let stream = TcpStream::connect((cfg.irc_host.as_str(), cfg.irc_port)).await?;
    let (rd, mut wr) = stream.into_split();
    let mut reader = BufReader::new(rd);

    // Register, handling nick collisions (433) by trying a numbered variant.
    let mut nick = base_nick.to_string();
    if !cfg.irc_pass.is_empty() {
        send_line(&mut wr, &format!("PASS {}", cfg.irc_pass)).await?;
    }
    send_line(&mut wr, &format!("NICK {nick}")).await?;
    send_line(&mut wr, &format!("USER {nick} 0 * :web user")).await?;

    let mut attempt = 0u32;
    let mut line = String::new();
    loop {
        line.clear();
        let n = timeout(Duration::from_secs(10), reader.read_line(&mut line)).await??;
        if n == 0 {
            return Err("closed during registration".into());
        }
        let raw = line.trim_end_matches(['\r', '\n']);
        if let Some(tok) = raw.strip_prefix("PING ") {
            send_line(&mut wr, &format!("PONG {tok}")).await?;
        } else if raw.contains(" 001 ") {
            break; // registered
        } else if raw.contains(" 433 ") || raw.contains(" 432 ") {
            attempt += 1;
            if attempt > 6 {
                return Err("could not acquire a nick".into());
            }
            nick = nick_variant(base_nick, attempt);
            send_line(&mut wr, &format!("NICK {nick}")).await?;
        }
    }

    let mut joined: HashSet<String> = HashSet::new();
    // Active loop: deliver queued messages, answer PINGs, quit when idle.
    loop {
        line.clear();
        tokio::select! {
            r = reader.read_line(&mut line) => {
                let n = r?;
                if n == 0 { break; }
                if let Some(tok) = line.trim_end_matches(['\r','\n']).strip_prefix("PING ") {
                    send_line(&mut wr, &format!("PONG {tok}")).await?;
                }
                // everything else (channel traffic) is ignored — puppets are write-only
            }
            m = timeout(cfg.puppet_ttl, rx.recv()) => {
                match m {
                    Err(_) | Ok(None) => {           // idle TTL, or hub dropped us
                        let _ = send_line(&mut wr, "QUIT :idle").await;
                        break;
                    }
                    Ok(Some(msg)) => {
                        if joined.insert(msg.channel.clone()) {
                            send_line(&mut wr, &format!("JOIN {}", msg.channel)).await?;
                        }
                        for chunk in chunk_irc(&msg.text) {
                            send_line(&mut wr, &format!("PRIVMSG {} :{chunk}", msg.channel)).await?;
                        }
                    }
                }
            }
        }
    }
    Ok(())
}

/// Derive a valid IRC nick (≤9, valid first char) from a web user id.
fn derive_nick(from: &str) -> String {
    let ok_first = |c: char| c.is_ascii_alphabetic() || "[]{}\\|^_".contains(c);
    let ok_rest = |c: char| c.is_ascii_alphanumeric() || "[]{}\\|^_-".contains(c);
    let mut out = String::new();
    for c in from.chars() {
        if out.is_empty() {
            if ok_first(c) {
                out.push(c);
            }
        } else if ok_rest(c) && out.len() < 9 {
            out.push(c);
        }
    }
    if out.is_empty() {
        out.push_str("web");
    }
    out
}

/// A numbered variant that still fits in 9 chars: "alice" + 3 → "alice3".
fn nick_variant(base: &str, n: u32) -> String {
    let suffix = n.to_string();
    let keep = 9usize.saturating_sub(suffix.len());
    let head = if base.len() > keep { &base[..keep] } else { base };
    format!("{head}{suffix}")
}

/// Render a non-chat (system/CDC) event as one IRC line.
fn render(etype: &str, payload: Option<&Value>) -> String {
    let body = payload.map(Value::to_string).unwrap_or_default();
    format!("[{etype}] {body}")
}

/// Pick the target channel: `irc-in/<chan>` → #<chan>; else payload.channel;
/// else the configured default.
fn target_channel(cfg: &Config, topic: &str, payload: Option<&Value>) -> String {
    if let Some(rest) = topic.strip_prefix("irc-in/") {
        let seg = rest.split('/').next().unwrap_or("");
        if seg.is_empty() {
            cfg.default_channel.clone()
        } else {
            hashed(seg)
        }
    } else if let Some(c) = payload.and_then(|p| p.get("channel")).and_then(Value::as_str) {
        hashed(c)
    } else {
        cfg.default_channel.clone()
    }
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

async fn send_line(w: &mut OwnedWriteHalf, line: &str) -> std::io::Result<()> {
    w.write_all(line.as_bytes()).await?;
    w.write_all(b"\r\n").await
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
