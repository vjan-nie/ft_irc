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
	  _epollFd(-1),
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
	if (_epollFd >= 0)
		close(_epollFd);
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
	_epollFd = epoll_create1(0);
	if (_epollFd < 0)
		throw std::runtime_error("epoll_create1() failed: " + std::string(strerror(errno)));
}

void Server::addToEpoll(int fd, uint32_t events)
{
	struct epoll_event ev;
	std::memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev) < 0)
		throw std::runtime_error("epoll_ctl ADD failed: " + std::string(strerror(errno)));
}

void Server::modifyEpoll(int fd, uint32_t events)
{
	struct epoll_event ev;
	std::memset(&ev, 0, sizeof(ev));
	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &ev) < 0)
		Log::error(std::string("epoll_ctl MOD failed: ") + strerror(errno));
}

void Server::removeFromEpoll(int fd)
{
	if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL) < 0
		&& errno != ENOENT && errno != EBADF)
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
		int nfds = epoll_wait(_epollFd, events, MAX_EVENTS, 1000);
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
					disconnectClient(fd, "Connection error");
			}
			else if (!dispatchExtensionFd(fd, ev))
			{
				/* fd belongs to nobody (raced a disconnect) — drop it */
				removeFromEpoll(fd);
			}
		}

		checkTimeouts();

		time_t now = std::time(NULL);
		for (size_t i = 0; i < _extensions.size(); ++i)
			_extensions[i]->onTick(*this, now);
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
	{
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			Log::error(std::string("accept() failed: ") + strerror(errno));
		return;
	}

	// Connection cap: reject gracefully instead of exhausting fds
	if (_clients.size() >= MAX_CLIENTS)
	{
		const char rejection[] = "ERROR :Server full\r\n";
		send(clientFd, rejection, sizeof(rejection) - 1, 0); // best effort
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

	addToEpoll(clientFd, EPOLLIN | EPOLLOUT);

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
		if (bytesRead == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
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
		// Check if client was disconnected during handling
		if (_clients.find(fd) == _clients.end())
			return;
	}

	// SendQ sweep: never disconnect mid-broadcast, only between events
	if (client->isSendQExceeded())
		disconnectClient(fd, "SendQ exceeded");
}

void Server::handleClientOutput(int fd)
{
	if (_clients.find(fd) == _clients.end())
		return;

	Client *client = _clients[fd];
	if (!client->hasPendingData())
		return;

	const std::string &buf = client->getSendBuffer();
	ssize_t bytesSent = send(fd, buf.c_str(), buf.size(), 0);
	if (bytesSent < 0)
	{
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			disconnectClient(fd, "Send error");
		return;
	}
	client->clearSendBuffer(bytesSent);

	// SendQ sweep: the peer is too slow / flooded; its stream already
	// lost messages, so terminate it cleanly.
	if (client->isSendQExceeded())
		disconnectClient(fd, "SendQ exceeded");
}

void Server::handleMessage(Client *client, const std::string &raw)
{
	Message msg = Message::parse(raw);
	if (msg.command.empty())
		return;

	dispatchCommand(client, msg);
}

void Server::checkTimeouts()
{
	time_t now = std::time(NULL);
	if (now - _lastPingCheck < 30)
		return;
	_lastPingCheck = now;

	std::vector<int> toDisconnect;
	for (std::map<int, Client *>::iterator it = _clients.begin();
		 it != _clients.end(); ++it)
	{
		Client *client = it->second;
		time_t idle = now - client->getLastActivity();

		if (client->isSendQExceeded())
		{
			toDisconnect.push_back(it->first);
		}
		else if (client->isPingSent() && idle > PING_INTERVAL + PING_TIMEOUT)
		{
			toDisconnect.push_back(it->first);
		}
		else if (!client->isPingSent() && idle > PING_INTERVAL)
		{
			sendToClient(client, "PING :" + _serverName);
			client->setPingSent(true);
		}
	}
	for (size_t i = 0; i < toDisconnect.size(); ++i)
		disconnectClient(toDisconnect[i], "Ping timeout");
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

void Server::disconnectClient(int fd, const std::string &reason)
{
	if (_clients.find(fd) == _clients.end())
		return;

	Client *client = _clients[fd];
	std::string prefix = client->getPrefix();
	std::string quitMsg = ":" + prefix + " QUIT :" + reason;

	// Broadcast QUIT to all channels the client is in, and remove them
	for (std::map<std::string, Channel *>::iterator it = _channels.begin();
		 it != _channels.end();)
	{
		Channel *chan = it->second;
		if (chan->isMember(client))
		{
			chan->broadcastMessage(quitMsg, client);
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

	removeFromEpoll(fd);
	close(fd);
	Log::info("client disconnected: " + client->getNickname()
			  + " (" + reason + ")");
	audit("disconnect", client->getNickname(), reason);
	delete client;
	_clients.erase(fd);
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
