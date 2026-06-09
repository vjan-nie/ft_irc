#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <map>
# include <vector>
# include <sys/epoll.h>

# include "Client.hpp"
# include "Channel.hpp"
# include "Message.hpp"
# include "Replies.hpp"

class Bot;
class PlatformBus;

class Server
{
	friend class PlatformBus; /* may register fds into the shared epoll */

public:
	Server(int port, const std::string &password);
	~Server();

	void	run();
	void	shutdown();

	/* ─── Client management ─── */
	Client	*findClientByNick(const std::string &nickname) const;
	void	sendToClient(Client *client, const std::string &msg);
	void	disconnectClient(int fd, const std::string &reason);

	/* ─── Channel management ─── */
	Channel	*findChannel(const std::string &name) const;
	Channel	*createChannel(const std::string &name, Client *creator);
	void	removeChannel(const std::string &name);

	/* ─── Getters ─── */
	const std::string	&getPassword() const;
	const std::string	&getServerName() const;
	Bot					*getBot() const;

	/* ─── Reply helpers ─── */
	void	sendReply(Client *client, const std::string &numeric,
					const std::string &params);

	/* ─── Static ─── */
	static bool	isRunning;

private:
	Server();
	Server(const Server &other);
	Server &operator=(const Server &other);

	/* ─── Socket setup ─── */
	void	createListenSocket();
	void	createEpoll();
	void	addToEpoll(int fd, uint32_t events);
	void	modifyEpoll(int fd, uint32_t events);
	void	removeFromEpoll(int fd);

	/* ─── Event handling ─── */
	void	acceptClient();
	void	handleClientInput(int fd);
	void	handleClientOutput(int fd);
	void	handleMessage(Client *client, const std::string &raw);
	void	checkTimeouts();

	/* ─── Command dispatch ─── */
	void	dispatchCommand(Client *client, const Message &msg);

	/* ─── Registration commands ─── */
	void	cmdCap(Client *client, const Message &msg);
	void	cmdPass(Client *client, const Message &msg);
	void	cmdNick(Client *client, const Message &msg);
	void	cmdUser(Client *client, const Message &msg);
	void	completeRegistration(Client *client);

	/* ─── Channel commands ─── */
	void	cmdJoin(Client *client, const Message &msg);
	void	cmdPart(Client *client, const Message &msg);
	void	cmdKick(Client *client, const Message &msg);
	void	cmdInvite(Client *client, const Message &msg);
	void	cmdTopic(Client *client, const Message &msg);
	void	cmdMode(Client *client, const Message &msg);

	/* ─── Messaging commands ─── */
	void	cmdPrivmsg(Client *client, const Message &msg);
	void	cmdNotice(Client *client, const Message &msg);
	void	cmdPing(Client *client, const Message &msg);
	void	cmdPong(Client *client, const Message &msg);
	void	cmdQuit(Client *client, const Message &msg);

	/* ─── Mode helpers ─── */
	void	handleUserMode(Client *client, const Message &msg);
	void	handleChannelMode(Client *client, Channel *channel,
							const Message &msg);

	/* ─── Query commands ─── */
	void	cmdWho(Client *client, const Message &msg);
	void	cmdWhois(Client *client, const Message &msg);
	void	cmdUserhost(Client *client, const Message &msg);

	/* ─── Utility ─── */
	bool	isValidNickname(const std::string &nick) const;
	bool	isValidChannelName(const std::string &name) const;
	void	broadcastToChannels(Client *client, const std::string &msg);

	/* ─── Platform bus (optional, config-gated) ─── */
	void	setupPlatformBus();

	/* ─── Data ─── */
	int							_port;
	std::string					_password;
	std::string					_serverName;
	int							_listenFd;
	int							_epollFd;
	std::map<int, Client *>		_clients;
	std::map<std::string, Channel *>	_channels;
	Bot							*_bot;
	PlatformBus					*_bus;
	time_t						_lastPingCheck;

	static const int			MAX_EVENTS = 64;
};

#endif
