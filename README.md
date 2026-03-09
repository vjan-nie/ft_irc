*This project has been created as part of the 42 curriculum by dlesieur.*

# ft_irc

## Description

ft_irc is an IRC (Internet Relay Chat) server implemented in C++98. It follows the IRC protocol (RFC 2812) and is designed to work with HexChat as the reference client. The server handles multiple simultaneous clients using non-blocking I/O with `epoll()`, supports authentication, channel management, private messaging, and operator commands.

### Features

- **Authentication**: Password-protected server with PASS/NICK/USER registration
- **Channels**: Create, join, and manage IRC channels with `#` prefix
- **Private messaging**: PRIVMSG and NOTICE between users
- **Channel operators**: KICK, INVITE, TOPIC, and MODE commands
- **Channel modes**: `+i` (invite-only), `+t` (topic restricted), `+k` (key/password), `+o` (operator), `+l` (user limit)
- **Bonus: Bot** — Built-in `ircbot` that responds to `!help`, `!time`, `!info`, `!joke`
- **Bonus: DCC** — Relays DCC file transfer handshake between clients
- **Partial message reassembly**: Handles fragmented TCP data correctly
- **Ping/Pong keepalive**: Automatic timeout detection

## Instructions

### Compilation

```bash
make        # Build the ircserv binary
make clean  # Remove object files
make fclean # Remove object files and binary
make re     # Full rebuild
```

### Running

```bash
./ircserv <port> <password>
```

- `port`: The TCP port to listen on (e.g., 6667)
- `password`: Connection password required by clients

### Connecting with HexChat

1. Open HexChat → Add a new network
2. Set server address to `127.0.0.1/6667`
3. Set the server password to the password you used when starting `ircserv`
4. Connect — you should see the welcome message

### Testing with netcat

```bash
nc -C 127.0.0.1 6667
PASS mypassword
NICK mynick
USER myuser 0 * :My Real Name
JOIN #test
PRIVMSG #test :Hello!
QUIT :bye
```

## Resources

- [RFC 2812 — Internet Relay Chat: Client Protocol](https://datatracker.ietf.org/doc/html/rfc2812)
- [RFC 1459 — Internet Relay Chat Protocol](https://datatracker.ietf.org/doc/html/rfc1459)
- [Modern IRC documentation](https://modern.ircdocs.horse/)
- [IRCv3 Specifications](https://ircv3.net/irc/)
- [HexChat documentation](https://hexchat.readthedocs.io/)
- [epoll(7) man page](https://man7.org/linux/man-pages/man7/epoll.7.html)

### AI Usage

AI (GitHub Copilot with Claude) was used as a programming assistant for:
- Planning the project architecture and identifying required IRC numerics for HexChat compatibility
- Generating boilerplate code for class declarations and socket setup
- Implementing IRC command handlers based on RFC 2812 specifications
- Debugging protocol compliance issues during testing
