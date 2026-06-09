//! ai-assistant — an IRC companion that replies via Claude when addressed.
//!
//! It connects to `ircserv` as an ordinary IRC client (`assistant`), joins the
//! configured channels, and—only when a message is addressed to it (`!ai ...`,
//! `assistant: ...`, or a direct PRIVMSG)—asks Claude and posts the reply.
//!
//! Configuration via environment variables:
//!   IRC_HOST           (default 127.0.0.1)
//!   IRC_PORT           (default 6667)
//!   IRC_PASS           (server password; required by ircserv)
//!   IRC_NICK           (default "assistant")
//!   IRC_CHANNELS       (comma-separated, e.g. "#general,#support")
//!   ANTHROPIC_API_KEY  (required)
//!   ANTHROPIC_MODEL    (default "claude-opus-4-8")
//!
//! The Claude call uses the raw Messages API over HTTPS (no official Rust SDK).

use std::collections::HashMap;
use std::sync::Arc;

use serde_json::json;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::TcpStream;
use tokio::sync::{mpsc, Mutex};

const ANTHROPIC_URL: &str = "https://api.anthropic.com/v1/messages";
const ANTHROPIC_VERSION: &str = "2023-06-01";
const HISTORY_PER_CHANNEL: usize = 8;
const IRC_MAX_MSG: usize = 400; // conservative; leaves room for the IRC envelope

#[derive(Clone)]
struct Config {
    host: String,
    port: u16,
    pass: String,
    nick: String,
    channels: Vec<String>,
    api_key: String,
    model: String,
}

impl Config {
    fn from_env() -> Result<Self, String> {
        let api_key = std::env::var("ANTHROPIC_API_KEY")
            .map_err(|_| "ANTHROPIC_API_KEY is required".to_string())?;
        let channels = std::env::var("IRC_CHANNELS")
            .unwrap_or_default()
            .split(',')
            .map(str::trim)
            .filter(|s| !s.is_empty())
            .map(String::from)
            .collect();
        Ok(Self {
            host: std::env::var("IRC_HOST").unwrap_or_else(|_| "127.0.0.1".to_string()),
            port: std::env::var("IRC_PORT")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(6667),
            pass: std::env::var("IRC_PASS").unwrap_or_default(),
            nick: std::env::var("IRC_NICK").unwrap_or_else(|_| "assistant".to_string()),
            channels,
            api_key,
            model: std::env::var("ANTHROPIC_MODEL")
                .unwrap_or_else(|_| "claude-opus-4-8".to_string()),
        })
    }
}

/// One stored conversational turn for a channel's context.
#[derive(Clone)]
struct Turn {
    role: &'static str, // "user" | "assistant"
    text: String,
}

type History = Arc<Mutex<HashMap<String, Vec<Turn>>>>;

#[tokio::main]
async fn main() {
    let cfg = match Config::from_env() {
        Ok(c) => c,
        Err(e) => {
            eprintln!("config error: {e}");
            std::process::exit(1);
        }
    };

    loop {
        if let Err(e) = run(&cfg).await {
            eprintln!("session error: {e}; reconnecting in 3s");
            tokio::time::sleep(std::time::Duration::from_secs(3)).await;
        }
    }
}

async fn run(cfg: &Config) -> Result<(), Box<dyn std::error::Error>> {
    let stream = TcpStream::connect((cfg.host.as_str(), cfg.port)).await?;
    let (read_half, write_half) = stream.into_split();
    let mut reader = BufReader::new(read_half);

    // All outbound IRC lines funnel through this channel to a single writer task,
    // so Claude calls (spawned tasks) never block PING/PONG.
    let (out_tx, mut out_rx) = mpsc::channel::<String>(256);
    tokio::spawn(async move {
        let mut w = write_half;
        while let Some(line) = out_rx.recv().await {
            if w.write_all(line.as_bytes()).await.is_err() {
                break;
            }
            if w.write_all(b"\r\n").await.is_err() {
                break;
            }
        }
    });

    // Registration.
    if !cfg.pass.is_empty() {
        out_tx.send(format!("PASS {}", cfg.pass)).await?;
    }
    out_tx.send(format!("NICK {}", cfg.nick)).await?;
    out_tx
        .send(format!("USER {} 0 * :AI Assistant", cfg.nick))
        .await?;
    for ch in &cfg.channels {
        out_tx.send(format!("JOIN {ch}")).await?;
    }
    eprintln!("connected to {}:{} as {}", cfg.host, cfg.port, cfg.nick);

    let history: History = Arc::new(Mutex::new(HashMap::new()));
    let http = reqwest::Client::new();

    let mut line = String::new();
    loop {
        line.clear();
        let n = reader.read_line(&mut line).await?;
        if n == 0 {
            return Err("connection closed".into());
        }
        let raw = line.trim_end_matches(['\r', '\n']).to_string();
        if raw.is_empty() {
            continue;
        }

        if let Some(token) = raw.strip_prefix("PING ") {
            out_tx.send(format!("PONG {token}")).await?;
            continue;
        }

        if let Some(msg) = parse_privmsg(&raw) {
            if msg.sender.eq_ignore_ascii_case(&cfg.nick) {
                continue; // never react to our own messages
            }
            let is_channel = msg.target.starts_with('#');
            let addressed = if is_channel {
                addressed_query(&msg.text, &cfg.nick)
            } else {
                Some(msg.text.clone()) // a direct PRIVMSG is always for us
            };
            let Some(query) = addressed else { continue };
            if query.trim().is_empty() {
                continue;
            }

            // Reply destination: the channel for channel messages, else the sender.
            let dest = if is_channel {
                msg.target.clone()
            } else {
                msg.sender.clone()
            };

            let cfg2 = cfg.clone();
            let http2 = http.clone();
            let history2 = Arc::clone(&history);
            let out2 = out_tx.clone();
            let sender = msg.sender.clone();
            tokio::spawn(async move {
                handle_query(&cfg2, &http2, &history2, &out2, &dest, &sender, &query).await;
            });
        }
    }
}

/// Build context, call Claude, store the turn, and post the reply.
async fn handle_query(
    cfg: &Config,
    http: &reqwest::Client,
    history: &History,
    out: &mpsc::Sender<String>,
    dest: &str,
    sender: &str,
    query: &str,
) {
    // Append the user's turn and snapshot the recent context.
    let messages: Vec<Turn> = {
        let mut map = history.lock().await;
        let turns = map.entry(dest.to_string()).or_default();
        turns.push(Turn {
            role: "user",
            text: format!("{sender}: {query}"),
        });
        if turns.len() > HISTORY_PER_CHANNEL {
            let cut = turns.len() - HISTORY_PER_CHANNEL;
            turns.drain(0..cut);
        }
        turns.clone()
    };

    match ask_claude(cfg, http, &messages).await {
        Ok(reply) => {
            {
                let mut map = history.lock().await;
                if let Some(turns) = map.get_mut(dest) {
                    turns.push(Turn {
                        role: "assistant",
                        text: reply.clone(),
                    });
                }
            }
            for chunk in chunk_reply(&reply) {
                if out.send(format!("PRIVMSG {dest} :{chunk}")).await.is_err() {
                    break;
                }
            }
        }
        Err(e) => {
            eprintln!("claude error: {e}");
            let _ = out
                .send(format!("PRIVMSG {dest} :(assistant is unavailable right now)"))
                .await;
        }
    }
}

/// Call the Claude Messages API (raw HTTP) and return the concatenated text.
async fn ask_claude(
    cfg: &Config,
    http: &reqwest::Client,
    turns: &[Turn],
) -> Result<String, Box<dyn std::error::Error + Send + Sync>> {
    let messages: Vec<serde_json::Value> = turns
        .iter()
        .map(|t| json!({ "role": t.role, "content": t.text }))
        .collect();

    let body = json!({
        "model": cfg.model,
        "max_tokens": 1024,
        "system": "You are \"assistant\", a helpful AI participant in a team's IRC chat \
                   for their business platform. Answer concisely in plain text (no markdown), \
                   ideally 1-3 sentences suitable for a chat message. If you are unsure, say so briefly.",
        "messages": messages,
    });

    let resp = http
        .post(ANTHROPIC_URL)
        .header("x-api-key", &cfg.api_key)
        .header("anthropic-version", ANTHROPIC_VERSION)
        .header("content-type", "application/json")
        .json(&body)
        .send()
        .await?;

    let status = resp.status();
    let value: serde_json::Value = resp.json().await?;
    if !status.is_success() {
        let msg = value
            .get("error")
            .and_then(|e| e.get("message"))
            .and_then(|m| m.as_str())
            .unwrap_or("unknown error");
        return Err(format!("HTTP {status}: {msg}").into());
    }

    // Concatenate every text block in the response content array.
    let text = value
        .get("content")
        .and_then(|c| c.as_array())
        .map(|blocks| {
            blocks
                .iter()
                .filter(|b| b.get("type").and_then(|t| t.as_str()) == Some("text"))
                .filter_map(|b| b.get("text").and_then(|t| t.as_str()))
                .collect::<Vec<&str>>()
                .join(" ")
        })
        .unwrap_or_default();

    if text.trim().is_empty() {
        return Err("empty response from model".into());
    }
    Ok(text.trim().to_string())
}

/// If `text` is addressed to `nick` (`!ai ...`, `nick: ...`, `nick, ...`,
/// `nick ...`), return the stripped query. Otherwise `None`.
fn addressed_query(text: &str, nick: &str) -> Option<String> {
    let t = text.trim_start();
    if let Some(rest) = t.strip_prefix("!ai ") {
        return Some(rest.trim().to_string());
    }
    let lower = t.to_lowercase();
    let nick_lower = nick.to_lowercase();
    if let Some(rest) = lower.strip_prefix(&nick_lower) {
        // The char right after the nick must be a separator, not more letters.
        let sep = rest.chars().next();
        if matches!(sep, Some(':') | Some(',') | Some(' ')) {
            let cut = nick.len() + 1;
            if cut <= t.len() {
                return Some(t[cut..].trim().to_string());
            }
            return Some(String::new());
        }
    }
    None
}

/// Split a reply into IRC-sized chunks, preferring line breaks then length.
fn chunk_reply(reply: &str) -> Vec<String> {
    let mut out = Vec::new();
    for line in reply.split('\n') {
        let line = line.trim_end();
        if line.is_empty() {
            continue;
        }
        if line.len() <= IRC_MAX_MSG {
            out.push(line.to_string());
        } else {
            let mut buf = String::new();
            for word in line.split(' ') {
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
        }
    }
    if out.is_empty() {
        out.push(String::new());
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
    let command = parts.next()?;
    if !command.eq_ignore_ascii_case("PRIVMSG") {
        return None;
    }
    let args = parts.next()?;
    let (target, trailing) = args.split_once(" :")?;
    Some(PrivMsg {
        sender,
        target: target.trim().to_string(),
        text: trailing.to_string(),
    })
}
