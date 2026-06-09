#include "PlatformBus.hpp"
#include "Server.hpp"
#include "Channel.hpp"
#include "Log.hpp"
#include "libcpp/str/case.hpp"
#include "libcpp/str/format.hpp"

#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

PlatformBus::PlatformBus(Server *server, int port, const std::string &secret,
						 const std::string &serviceNick)
	: _server(server),
	  _port(port),
	  _secret(secret),
	  _serviceNick(serviceNick),
	  _listenFd(-1),
	  _conns()
{
}

PlatformBus::~PlatformBus()
{
	for (std::map<int, Conn>::iterator it = _conns.begin();
		 it != _conns.end(); ++it)
		close(it->first);
	if (_listenFd >= 0)
		close(_listenFd);
}

bool PlatformBus::start()
{
	_listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenFd < 0)
	{
		Log::warn(std::string("platform-bus: socket() failed: ") + strerror(errno));
		return false;
	}

	int opt = 1;
	setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) < 0)
	{
		Log::warn(std::string("platform-bus: fcntl() failed: ") + strerror(errno));
		close(_listenFd);
		_listenFd = -1;
		return false;
	}

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 only */
	addr.sin_port = htons(static_cast<uint16_t>(_port));

	if (bind(_listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
	{
		Log::warn(std::string("platform-bus: bind() failed: ") + strerror(errno));
		close(_listenFd);
		_listenFd = -1;
		return false;
	}
	if (listen(_listenFd, 16) < 0)
	{
		Log::warn(std::string("platform-bus: listen() failed: ") + strerror(errno));
		close(_listenFd);
		_listenFd = -1;
		return false;
	}

	_server->addToEpoll(_listenFd, EPOLLIN);
	Log::info("platform-bus listening on 127.0.0.1:" + libcpp::str::to_string(_port));
	return true;
}

int PlatformBus::listenFd() const
{
	return _listenFd;
}

bool PlatformBus::owns(int fd) const
{
	return _conns.find(fd) != _conns.end();
}

void PlatformBus::acceptConnection()
{
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	int fd = accept(_listenFd, reinterpret_cast<struct sockaddr *>(&addr), &len);
	if (fd < 0)
		return;

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
	{
		close(fd);
		return;
	}

	Conn conn;
	conn.authed = _secret.empty(); /* no secret configured -> loopback trust */
	_conns[fd] = conn;
	_server->addToEpoll(fd, EPOLLIN);
}

void PlatformBus::handleInput(int fd)
{
	std::map<int, Conn>::iterator it = _conns.find(fd);
	if (it == _conns.end())
		return;

	char buf[2048];
	ssize_t n = recv(fd, buf, sizeof(buf), 0);
	if (n <= 0)
	{
		if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			closeConnection(fd);
		return;
	}

	it->second.buffer.append(buf, static_cast<size_t>(n));

	std::string::size_type pos;
	while ((pos = it->second.buffer.find('\n')) != std::string::npos)
	{
		std::string line = it->second.buffer.substr(0, pos);
		it->second.buffer.erase(0, pos + 1);
		handleLine(fd, libcpp::str::trim(line));
		if (_conns.find(fd) == _conns.end()) /* closed mid-processing */
			return;
	}
}

void PlatformBus::closeConnection(int fd)
{
	if (_conns.find(fd) == _conns.end())
		return;
	_server->removeFromEpoll(fd);
	close(fd);
	_conns.erase(fd);
}

void PlatformBus::send(int fd, const std::string &text)
{
	std::string out = text + "\n";
	::send(fd, out.c_str(), out.size(), 0);
}

void PlatformBus::handleLine(int fd, const std::string &line)
{
	if (line.empty())
		return;

	std::string::size_type sp = line.find(' ');
	std::string cmd = libcpp::str::to_upper(sp == std::string::npos
											? line : line.substr(0, sp));
	std::string rest = (sp == std::string::npos) ? "" : line.substr(sp + 1);

	Conn &conn = _conns[fd];

	if (cmd == "PING")
	{
		send(fd, "PONG");
		return;
	}
	if (cmd == "AUTH")
	{
		if (!_secret.empty() && libcpp::str::trim(rest) == _secret)
		{
			conn.authed = true;
			send(fd, "OK authenticated");
		}
		else
			send(fd, "ERR bad secret");
		return;
	}
	if (!conn.authed)
	{
		send(fd, "ERR not authenticated");
		return;
	}
	if (cmd == "PUB")
	{
		/* rest = "<#channel> <type> :<message>" */
		std::string::size_type s1 = rest.find(' ');
		if (s1 == std::string::npos)
		{
			send(fd, "ERR usage: PUB <#channel> <type> :<message>");
			return;
		}
		std::string channel = rest.substr(0, s1);
		std::string tail = rest.substr(s1 + 1);

		std::string type;
		std::string message;
		std::string::size_type colon = tail.find(" :");
		if (colon == std::string::npos)
		{
			type = libcpp::str::trim(tail);
		}
		else
		{
			type = libcpp::str::trim(tail.substr(0, colon));
			message = tail.substr(colon + 2);
		}
		publish(channel, type, message);
		send(fd, "OK");
		return;
	}
	send(fd, "ERR unknown command");
}

void PlatformBus::publish(const std::string &channel, const std::string &type,
						  const std::string &message)
{
	Channel *chan = _server->findChannel(channel);
	if (!chan)
		return; /* nobody is listening on that channel yet */

	std::string body = message;
	if (!type.empty())
		body = "[" + type + "] " + message;

	std::string line = ":" + _serviceNick + "!svc@" + _server->getServerName()
					 + " PRIVMSG " + channel + " :" + body;
	chan->broadcastMessage(line, NULL);
}
