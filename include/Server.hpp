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
# include "libcpp98/reactor.hpp"

class IServerExtension;

class Server
{
public:
	/* pendingCloseTimeoutSec defaults to PENDING_CLOSE_TIMEOUT (the
	** production value); tests can pass a smaller value so the deadline
	** sweep (checkPendingCloseTimeouts()) doesn't cost real wall-clock
	** seconds per run. */
	Server(int port, const std::string &password,
		   time_t pendingCloseTimeoutSec = PENDING_CLOSE_TIMEOUT);
	~Server();

	void	run();
	void	shutdown();

	/* ─── Client management ─── */
	Client	*findClientByNick(const std::string &nickname) const;
	Client	*findClientByFd(int fd) const;
	void	sendToClient(Client *client, const std::string &msg);
	void	disconnectClient(int fd, const std::string &reason);

	/* ─── Channel management ─── */
	Channel	*findChannel(const std::string &name) const;
	Channel	*createChannel(const std::string &name, Client *creator);
	void	removeChannel(const std::string &name);

	/* ─── Getters ─── */
	const std::string	&getPassword() const;
	const std::string	&getServerName() const;

	/* ─── Reply helpers ─── */
	void	sendReply(Client *client, const std::string &numeric,
					const std::string &params);

	/* ─── Audit hook (fans out to extensions; no-op when none listen) ─── */
	void	audit(const std::string &event, const std::string &actor,
				const std::string &detail);

	/* ─── Extension seam ─── */
	/* Server takes ownership; extensions are deleted in reverse order. */
	void	addExtension(IServerExtension *ext);
	/* Lets an extension multiplex its own fds into the single epoll. */
	bool	registerExternalFd(int fd, uint32_t events);
	void	unregisterExternalFd(int fd);

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
	void	checkPendingCloseTimeouts();
	void	updateEpollInterest(Client *client);
	bool	dispatchExtensionFd(int fd, uint32_t events);

	/* ─── Disconnect teardown ─── */
	/* Logical goodbye (QUIT to peers, extension fan-out, log/audit) --
	** shared by the deferred and immediate close paths so it never runs
	** twice for the same client. */
	void	teardownClientState(Client *client, const std::string &reason);
	/* Immediate close: for cases where waiting to drain _out would be
	** wrong (SendQ already exceeded, socket already errored). */
	void	disconnectClientNow(int fd, const std::string &reason);
	/* Physical close only -- no re-notification. Used once teardown has
	** already run (drain completed, or the pending-close deadline hit). */
	void	finalizeDisconnect(int fd);

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

	/* ─── Data ─── */
	int							_port;
	std::string					_password;
	std::string					_serverName;
	int							_listenFd;
	/* epoll lifecycle + checked ctl ops; the single epoll_wait() call
	** stays below in run() (src/Server.cpp). */
	libcpp98::Reactor			_reactor;
	std::map<int, Client *>		_clients;
	std::map<int, uint32_t>		_epollMask;
	std::map<std::string, Channel *>	_channels;
	std::vector<IServerExtension *>	_extensions;
	time_t						_lastPingCheck;
	time_t						_pendingCloseTimeoutSec;

	static const int			MAX_EVENTS = 64;
};

#endif
