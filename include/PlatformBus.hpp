#ifndef PLATFORMBUS_HPP
# define PLATFORMBUS_HPP

# include <string>
# include <map>

class Server;

/*
** PlatformBus — the one original in-binary feature.
**
** A second listening socket bound to 127.0.0.1 (loopback only), multiplexed in
** the SAME epoll as the IRC listener (no extra poll() — subject-legal). It lets
** a local platform backend push real-time events that get injected into IRC
** channels as a virtual "service" participant, exactly like the Bot does.
**
** It is OPT-IN: created only when a config file enables it (see Server). The
** plain `./ircserv <port> <password>` invocation never opens it, so the graded
** RFC behaviour is unchanged.
**
** Loopback TCP (not a Unix-domain socket) is deliberate: it uses only the
** socket calls the subject already allows (socket/bind/listen/accept), with no
** unlink()/chmod().
**
** Line protocol (one command per line, '\n' terminated):
**   AUTH <secret>                     authenticate the connection (if a secret
**                                     is configured)
**   PUB <#channel> <type> :<message>  inject an event into a channel
**   PING                              liveness check -> "PONG"
*/
class PlatformBus
{
public:
	PlatformBus(Server *server, int port, const std::string &secret,
				const std::string &serviceNick);
	~PlatformBus();

	bool	start();                       /* bind+listen on 127.0.0.1:port */
	int		listenFd() const;
	bool	owns(int fd) const;            /* is fd a bus connection? */
	void	acceptConnection();
	void	handleInput(int fd);
	void	closeConnection(int fd);

private:
	PlatformBus();
	PlatformBus(const PlatformBus &other);
	PlatformBus &operator=(const PlatformBus &other);

	struct Conn
	{
		bool		authed;
		std::string	buffer;

		Conn() : authed(false), buffer() {}
	};

	void	handleLine(int fd, const std::string &line);
	void	send(int fd, const std::string &text);
	void	publish(const std::string &channel, const std::string &type,
					const std::string &message);

	Server				*_server;
	int					_port;
	std::string			_secret;
	std::string			_serviceNick;
	int					_listenFd;
	std::map<int, Conn>	_conns;
};

#endif /* PLATFORMBUS_HPP */
