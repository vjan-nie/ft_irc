#include "Server.hpp"
#include "IrcCase.hpp"
#include "Log.hpp"
#include "ext/IServerExtension.hpp"
#include "libcpp/str/format.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <new>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

bool Server::isRunning = true;

Server::Server(int port, const std::string &password)
	: _port(port),
	  _password(password),
	  _serverName(SERVER_NAME),
	  _listenFd(-1),
	  _reactor(),
	  _lastPingCheck(std::time(NULL))
{
	createListenSocket();
	createEpoll();
	addToEpoll(_listenFd, EPOLLIN);
}

void Server::audit(const std::string &event, const std::string &actor,
				   const std::string &detail)
{
	for (size_t i = 0; i < _extensions.size(); ++i)
		_extensions[i]->onAudit(event, actor, detail);
}

/* ─── Extension seam ─── */

void Server::addExtension(IServerExtension *ext)
{
	if (ext)
		_extensions.push_back(ext);
}

bool Server::registerExternalFd(int fd, uint32_t events)
{
	try
	{
		addToEpoll(fd, events);
	}
	catch (const std::exception &)
	{
		return false;
	}
	return true;
}

void Server::unregisterExternalFd(int fd)
{
	removeFromEpoll(fd);
}

Server::~Server()
{
	for (std::vector<IServerExtension *>::reverse_iterator it =
			 _extensions.rbegin(); it != _extensions.rend(); ++it)
		delete *it;
	for (std::map<int, Client *>::iterator it = _clients.begin();
		 it != _clients.end(); ++it)
	{
		close(it->first);
		delete it->second;
	}
	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		 it != _channels.end(); ++it)
	{
		delete it->second;
	}
	/* _reactor closes its epoll fd itself */
	if (_listenFd >= 0)
		close(_listenFd);
}

/* ─── Socket setup ─── */

void Server::createListenSocket()
{
	_listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenFd < 0)
		throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));

	int opt = 1;
	if (setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		throw std::runtime_error("setsockopt() failed: " + std::string(strerror(errno)));

	// Set non-blocking
	if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl() failed: " + std::string(strerror(errno)));

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(_port);

	if (bind(_listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
		throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

	if (listen(_listenFd, SOMAXCONN) < 0)
		throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));
}

void Server::createEpoll()
{
	if (!_reactor.open())
		throw std::runtime_error("epoll_create1() failed: " + std::string(strerror(errno)));
}

void Server::addToEpoll(int fd, uint32_t events)
{
	if (!_reactor.add(fd, events))
		throw std::runtime_error("epoll_ctl ADD failed: " + std::string(strerror(errno)));
}

void Server::modifyEpoll(int fd, uint32_t events)
{
	if (!_reactor.modify(fd, events))
		Log::error(std::string("epoll_ctl MOD failed: ") + strerror(errno));
}

void Server::removeFromEpoll(int fd)
{
	if (!_reactor.remove(fd) && errno != ENOENT && errno != EBADF)
		Log::error(std::string("epoll_ctl DEL failed: ") + strerror(errno));
}

/* ─── Main event loop ─── */

void Server::run()
{
	struct epoll_event events[MAX_EVENTS];

	Log::banner("ft_irc - listening on port " + libcpp::str::to_string(_port));

	for (size_t i = 0; i < _extensions.size(); ++i)
		_extensions[i]->onServerStart(*this);

	while (isRunning)
	{
		/* The subject's single poll-equivalent call site. */
		int nfds = epoll_wait(_reactor.fd(), events, MAX_EVENTS, 1000);
		if (nfds < 0)
		{
			if (errno == EINTR)
				continue;
			throw std::runtime_error("epoll_wait() failed: " + std::string(strerror(errno)));
		}

		for (int i = 0; i < nfds; ++i)
		{
			int fd = events[i].data.fd;
			uint32_t ev = events[i].events;

			if (fd == _listenFd)
			{
				acceptClient();
			}
			else if (_clients.count(fd))
			{
				if (ev & EPOLLIN)
					handleClientInput(fd);
				if ((ev & EPOLLOUT) && _clients.count(fd))
					handleClientOutput(fd);
				if ((ev & (EPOLLERR | EPOLLHUP)) && _clients.count(fd))
					// Immediate: the socket already signaled an error,
					// draining into it would be futile at best.
					disconnectClientNow(fd, "Connection error");
			}
			else if (!dispatchExtensionFd(fd, ev))
			{
				/* fd belongs to nobody (raced a disconnect) — drop it */
				removeFromEpoll(fd);
			}
		}

		checkTimeouts();
		checkPendingCloseTimeouts();

		time_t now = std::time(NULL);
		for (size_t i = 0; i < _extensions.size(); ++i)
			_extensions[i]->onTick(*this, now);

		for (std::map<int, Client *>::iterator it = _clients.begin();
			 it != _clients.end(); ++it)
			updateEpollInterest(it->second);
	}
}

/* Route an epoll event to the extension owning that fd (if any). */
bool Server::dispatchExtensionFd(int fd, uint32_t events)
{
	for (size_t i = 0; i < _extensions.size(); ++i)
	{
		if (_extensions[i]->ownsFd(fd))
		{
			_extensions[i]->onFdEvent(*this, fd, events);
			return true;
		}
	}
	return false;
}

void Server::shutdown()
{
	isRunning = false;
}

/* ─── Event handlers ─── */

void Server::acceptClient()
{
	struct sockaddr_in clientAddr;
	socklen_t addrLen = sizeof(clientAddr);

	int clientFd = accept(_listenFd,
						  reinterpret_cast<struct sockaddr *>(&clientAddr),
						  &addrLen);
	if (clientFd < 0)
		return;

	// Connection cap: reject gracefully instead of exhausting fds
	if (_clients.size() >= MAX_CLIENTS)
	{
	    /* Reject by closing only. This does NOT go through the deferred-
	    ** teardown mechanism (disconnectClient/finalizeDisconnect, T4): this
	    ** client never enters _clients, so it's invisible to the reconcile
	    ** sweep and checkPendingCloseTimeouts() that mechanism relies on.
	    ** A courtesy "Server full" line here is out of scope for T4 and
	    ** remains an accepted regression -- see CLAUDE.md. */
	    close(clientFd);
	    Log::warn("connection rejected: MAX_CLIENTS reached");
	    return;
	}

	// Set non-blocking
	if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0)
	{
		Log::error(std::string("fcntl() failed on client fd: ") + strerror(errno));
		close(clientFd);
		return;
	}

	std::string hostname = inet_ntoa(clientAddr.sin_addr);
	Client *client;
	try
	{
		client = new Client(clientFd, hostname);
	}
	catch (const std::bad_alloc &)
	{
		Log::error("out of memory: cannot accept new client");
		close(clientFd);
		return;
	}
	_clients[clientFd] = client;

	addToEpoll(clientFd, EPOLLIN);
	_epollMask[clientFd] = EPOLLIN;

	Log::info("new connection from " + hostname
			  + " (fd " + libcpp::str::to_string(clientFd) + ")");
}

void Server::handleClientInput(int fd)
{
	if (_clients.find(fd) == _clients.end())
		return;

	Client *client = _clients[fd];
	char buf[MAX_MSGLEN + 1];

	ssize_t bytesRead = recv(fd, buf, MAX_MSGLEN, 0);
	if (bytesRead <= 0)
	{
		if (bytesRead == 0)
			disconnectClient(fd, "Connection closed");
		return;
	}

	buf[bytesRead] = '\0';
	client->appendToRecvBuffer(std::string(buf, bytesRead));
	client->updateLastActivity();
	client->setPingSent(false);

	std::vector<std::string> messages = client->extractMessages();
	for (size_t i = 0; i < messages.size(); ++i)
	{
		handleMessage(client, messages[i]);
		// Check if the client was disconnected during handling, or just
		// marked pending-close (e.g. QUIT deferred behind unflushed data)
		// -- either way, stop feeding it more commands from this batch.
		std::map<int, Client *>::iterator cit = _clients.find(fd);
		if (cit == _clients.end() || cit->second->isPendingClose())
			return;
	}

	// SendQ sweep: never disconnect mid-broadcast, only between events.
	// Immediate: draining towards a peer that already blew SendQ would
	// recreate the T6 frozen-reader scenario.
	if (client->isSendQExceeded())
		disconnectClientNow(fd, "SendQ exceeded");
}

void Server::handleClientOutput(int fd)
{
	std::map<int, Client *>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return;

	Client *client = it->second;
	if (!client->hasPendingData())
		return;

	const std::string &buf = client->getSendBuffer();
	ssize_t bytesSent = send(fd, buf.c_str(), buf.size(), 0);
	if (bytesSent < 0)
		return;
	client->clearSendBuffer(bytesSent);

	if (client->isPendingClose())
	{
		/* Draining a deferred close: finish once _out empties. If it
		** blows SendQ mid-drain, don't linger waiting on a peer that
		** isn't reading (T6) -- close now. teardownClientState() already
		** ran when this client was marked, so no re-notification here. */
		if (!client->hasPendingData() || client->isSendQExceeded())
			finalizeDisconnect(fd);
		return;
	}

	// SendQ sweep: the peer is too slow / flooded; its stream already
	// lost messages, so terminate it cleanly. Immediate for the same
	// reason as above.
	if (client->isSendQExceeded())
		disconnectClientNow(fd, "SendQ exceeded");
}

void Server::handleMessage(Client *client, const std::string &raw)
{
	Message msg = Message::parse(raw);
	if (msg.command.empty())
		return;

	dispatchCommand(client, msg);
}

void Server::updateEpollInterest(Client *client)
{
	int fd = client->getFd();
	/* A pending-close client is write-only: no more input is ever read
	** for it, it's just draining towards finalizeDisconnect(). */
	uint32_t want = (client->isPendingClose() ? 0u : EPOLLIN)
				  | (client->hasPendingData() ? EPOLLOUT : 0u);
	std::map<int, uint32_t>::iterator it = _epollMask.find(fd);
	if (it != _epollMask.end() && it->second == want)
		return;
	modifyEpoll(fd, want);
	_epollMask[fd] = want;
}

void Server::checkTimeouts()
{
	time_t now = std::time(NULL);
	if (now - _lastPingCheck < 30)
		return;
	_lastPingCheck = now;

	std::vector<int> sendQNow;
	std::vector<int> pingTimeoutDeferred;
	for (std::map<int, Client *>::iterator it = _clients.begin();
		 it != _clients.end(); ++it)
	{
		Client *client = it->second;
		/* Already tearing down -- its fate is decided (drain or
		** checkPendingCloseTimeouts()), not re-evaluated here. */
		if (client->isPendingClose())
			continue;
		time_t idle = now - client->getLastActivity();

		if (client->isSendQExceeded())
		{
			sendQNow.push_back(it->first);
		}
		else if (client->isPingSent() && idle > PING_INTERVAL + PING_TIMEOUT)
		{
			pingTimeoutDeferred.push_back(it->first);
		}
		else if (!client->isPingSent() && idle > PING_INTERVAL)
		{
			sendToClient(client, "PING :" + _serverName);
			client->setPingSent(true);
		}
	}
	// Immediate: draining towards a peer that already blew SendQ would
	// recreate the T6 frozen-reader scenario.
	for (size_t i = 0; i < sendQNow.size(); ++i)
		disconnectClientNow(sendQNow[i], "SendQ exceeded");
	for (size_t i = 0; i < pingTimeoutDeferred.size(); ++i)
		disconnectClient(pingTimeoutDeferred[i], "Ping timeout");
}

/* Safety net for the deferred-close path: a client marked pending-close
** whose _out never drains (peer not reading) would otherwise sit in
** _clients forever, since updateEpollInterest() stops requesting EPOLLIN
** for it and a full send-buffer can mean EPOLLOUT never re-fires either.
** Runs every tick, unthrottled (unlike checkTimeouts()'s 30s gate) and
** off its own _pendingCloseSince timestamp, not _lastActivity -- the
** latter stops updating once EPOLLIN is stripped, which would make a
** deadline based on it never elapse. */
void Server::checkPendingCloseTimeouts()
{
	time_t now = std::time(NULL);
	std::vector<int> expired;
	for (std::map<int, Client *>::iterator it = _clients.begin();
		 it != _clients.end(); ++it)
	{
		if (it->second->isPendingClose()
			&& now - it->second->getPendingCloseSince() >= PENDING_CLOSE_TIMEOUT)
			expired.push_back(it->first);
	}
	for (size_t i = 0; i < expired.size(); ++i)
	{
		/* This peer never freed enough window to drain the backlog, so a
		** plain close() would leave the kernel trying to gracefully
		** flush it for an indeterminate time -- the whole point of the
		** deadline is to stop waiting on this peer, so force an
		** abortive close (RST, discard unsent data) instead of letting
		** the OS keep trying on our behalf after we've already given up. */
		struct linger lg;
		lg.l_onoff = 1;
		lg.l_linger = 0;
		setsockopt(expired[i], SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
		finalizeDisconnect(expired[i]);
	}
}

/* ─── Command dispatch ─── */

void Server::dispatchCommand(Client *client, const Message &msg)
{
	const std::string &cmd = msg.command;

	// Pre-registration commands (always allowed)
	if (cmd == "CAP")		{ cmdCap(client, msg); return; }
	if (cmd == "PASS")		{ cmdPass(client, msg); return; }
	if (cmd == "NICK")		{ cmdNick(client, msg); return; }
	if (cmd == "USER")		{ cmdUser(client, msg); return; }
	if (cmd == "QUIT")		{ cmdQuit(client, msg); return; }
	if (cmd == "PONG")		{ cmdPong(client, msg); return; }

	// Everything else requires registration
	if (!client->isRegistered())
	{
		sendReply(client, ERR_NOTREGISTERED, ":You have not registered");
		return;
	}

	if (cmd == "PING")		{ cmdPing(client, msg); return; }
	if (cmd == "JOIN")		{ cmdJoin(client, msg); return; }
	if (cmd == "PART")		{ cmdPart(client, msg); return; }
	if (cmd == "PRIVMSG")	{ cmdPrivmsg(client, msg); return; }
	if (cmd == "NOTICE")	{ cmdNotice(client, msg); return; }
	if (cmd == "KICK")		{ cmdKick(client, msg); return; }
	if (cmd == "INVITE")	{ cmdInvite(client, msg); return; }
	if (cmd == "TOPIC")		{ cmdTopic(client, msg); return; }
	if (cmd == "MODE")		{ cmdMode(client, msg); return; }
	if (cmd == "WHO")		{ cmdWho(client, msg); return; }
	if (cmd == "WHOIS")		{ cmdWhois(client, msg); return; }
	if (cmd == "USERHOST")	{ cmdUserhost(client, msg); return; }

	// Extensions may add commands here — after the core chain, so they can
	// never shadow an RFC command.
	for (size_t i = 0; i < _extensions.size(); ++i)
	{
		if (_extensions[i]->onCommand(*this, *client, msg))
			return;
	}

	sendReply(client, ERR_UNKNOWNCOMMAND, cmd + " :Unknown command");
}

/* ─── Client management ─── */

Client *Server::findClientByFd(int fd) const
{
	std::map<int, Client *>::const_iterator it = _clients.find(fd);
	return it == _clients.end() ? NULL : it->second;
}

Client *Server::findClientByNick(const std::string &nickname) const
{
	for (std::map<int, Client *>::const_iterator it = _clients.begin();
		 it != _clients.end(); ++it)
	{
		if (ircEquals(it->second->getNickname(), nickname))
			return it->second;
	}
	return NULL;
}

void Server::sendToClient(Client *client, const std::string &msg)
{
	client->queueMessage(":" + _serverName + " " + msg);
}

void Server::sendReply(Client *client, const std::string &numeric,
					   const std::string &params)
{
	client->queueMessage(":" + _serverName + " " + numeric + " "
						 + client->getNickname() + " " + params);
}

/* Logical goodbye: QUIT to channel peers (deduped by fd), leave channels,
** notify extensions, log/audit. Runs exactly once per client regardless of
** whether the physical close happens now or after draining -- callers
** must not call this twice for the same client. */
void Server::teardownClientState(Client *client, const std::string &reason)
{
	int fd = client->getFd();
	std::string prefix = client->getPrefix();
	std::string quitMsg = ":" + prefix + " QUIT :" + reason;

	// Broadcast QUIT to all channels the client is in, and remove them.
	// Dedup by fd across channels (same pattern as broadcastToChannels)
	// so a peer sharing N channels with the departing client gets the
	// QUIT line queued once, not N times.
	std::set<int> alreadySent;
	alreadySent.insert(fd);
	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		 it != _channels.end();)
	{
		Channel *chan = it->second;
		if (chan->isMember(client))
		{
			std::vector<Client *> members = chan->getMembers();
			for (size_t i = 0; i < members.size(); ++i)
			{
				int mfd = members[i]->getFd();
				if (alreadySent.count(mfd))
					continue;
				members[i]->queueMessage(quitMsg);
				alreadySent.insert(mfd);
			}
			chan->removeMember(client);
			if (chan->isEmpty())
			{
				delete chan;
				_channels.erase(it++);
				continue;
			}
		}
		++it;
	}

	for (size_t i = 0; i < _extensions.size(); ++i)
		_extensions[i]->onClientDisconnect(*this, *client, reason);

	Log::info("client disconnected: " + client->getNickname()
			  + " (" + reason + ")");
	audit("disconnect", client->getNickname(), reason);
}

/* Physical close only: epoll/fd/heap teardown, no re-notification. Used
** once teardownClientState() has already run for this client -- either
** just now (immediate path) or earlier, at the moment it was marked
** pending-close (deferred path, drain-complete or deadline). */
void Server::finalizeDisconnect(int fd)
{
	_epollMask.erase(fd);
	removeFromEpoll(fd);
	close(fd);
	delete _clients[fd];
	_clients.erase(fd);
}

/* Deferred close (default path): the client's departure is announced and
** it leaves its channels right away (teardownClientState), but the fd
** itself only closes once _out drains via the existing EPOLLOUT-gated
** handleClientOutput(), or PENDING_CLOSE_TIMEOUT elapses
** (checkPendingCloseTimeouts()) -- whichever comes first. This is what
** lets a reply queued right before disconnecting (e.g. 464 on password
** mismatch) actually reach the client instead of being discarded with the
** socket still holding it. */
void Server::disconnectClient(int fd, const std::string &reason)
{
	std::map<int, Client *>::iterator cit = _clients.find(fd);
	if (cit == _clients.end())
		return;

	Client *client = cit->second;
	if (client->isPendingClose())
		return; // already tearing down; the drain or the deadline finishes it

	teardownClientState(client, reason);
	if (!client->hasPendingData())
	{
		finalizeDisconnect(fd);
		return;
	}
	client->markPendingClose();
}

/* Immediate close, bypassing the drain: for SendQ-exceeded and socket
** errors, where waiting to flush _out would either write to an
** already-errored socket or recreate the T6 frozen-reader scenario against
** a peer that isn't consuming its backlog. */
void Server::disconnectClientNow(int fd, const std::string &reason)
{
	std::map<int, Client *>::iterator cit = _clients.find(fd);
	if (cit == _clients.end())
		return;

	Client *client = cit->second;
	if (client->isPendingClose())
	{
		// Already announced at mark time; just stop draining and close.
		finalizeDisconnect(fd);
		return;
	}
	teardownClientState(client, reason);
	finalizeDisconnect(fd);
}

/* ─── Channel management ─── */

/* Channels are keyed by their casemapped name; the original-case display
** name lives in Channel::_name. */
Channel *Server::findChannel(const std::string &name) const
{
	std::map<std::string, Channel *>::const_iterator it =
		_channels.find(ircToLower(name));
	if (it != _channels.end())
		return it->second;
	return NULL;
}

Channel *Server::createChannel(const std::string &name, Client *creator)
{
	Channel *channel;
	try
	{
		channel = new Channel(name, creator);
	}
	catch (const std::bad_alloc &)
	{
		Log::error("out of memory: cannot create channel " + name);
		return NULL;
	}
	_channels[ircToLower(name)] = channel;
	return channel;
}

void Server::removeChannel(const std::string &name)
{
	std::map<std::string, Channel *>::iterator it =
		_channels.find(ircToLower(name));
	if (it != _channels.end())
	{
		delete it->second;
		_channels.erase(it);
	}
}

/* ─── Getters ─── */

const std::string &Server::getPassword() const { return _password; }
const std::string &Server::getServerName() const { return _serverName; }

/* ─── Utility ─── */

bool Server::isValidNickname(const std::string &nick) const
{
	if (nick.empty() || nick.size() > MAX_NICKLEN)
		return false;

	// First char must be letter or special
	char first = nick[0];
	if (!std::isalpha(static_cast<unsigned char>(first))
		&& first != '[' && first != ']' && first != '{'
		&& first != '}' && first != '\\' && first != '|'
		&& first != '^' && first != '_')
		return false;

	for (size_t i = 1; i < nick.size(); ++i)
	{
		char c = nick[i];
		if (!std::isalnum(static_cast<unsigned char>(c))
			&& c != '[' && c != ']' && c != '{'
			&& c != '}' && c != '\\' && c != '|'
			&& c != '^' && c != '_' && c != '-')
			return false;
	}
	return true;
}

bool Server::isValidChannelName(const std::string &name) const
{
	if (name.empty() || name.size() > MAX_CHANNELLEN)
		return false;
	if (name[0] != '#')
		return false;
	if (name.size() < 2)
		return false;
	// No spaces, ctrl-G (BEL), or commas
	for (size_t i = 0; i < name.size(); ++i)
	{
		if (name[i] == ' ' || name[i] == '\x07' || name[i] == ',')
			return false;
	}
	return true;
}

void Server::broadcastToChannels(Client *client, const std::string &msg)
{
	std::set<int> alreadySent;
	alreadySent.insert(client->getFd());

	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		 it != _channels.end(); ++it)
	{
		Channel *chan = it->second;
		if (!chan->isMember(client))
			continue;
		std::vector<Client *> members = chan->getMembers();
		for (size_t i = 0; i < members.size(); ++i)
		{
			if (alreadySent.count(members[i]->getFd()))
				continue;
			members[i]->queueMessage(msg);
			alreadySent.insert(members[i]->getFd());
		}
	}
}
