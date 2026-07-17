#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <vector>
# include <ctime>

# include "libcpp98/buffered_socket.hpp"

class Client
{
public:
	Client(int fd, const std::string &hostname);
	~Client();

	/* ─── Getters ─── */
	int					getFd() const;
	const std::string	&getNickname() const;
	const std::string	&getUsername() const;
	const std::string	&getRealname() const;
	const std::string	&getHostname() const;
	const std::string	&getPassword() const;
	bool				isAuthenticated() const;
	bool				isRegistered() const;
	bool				hasPassSent() const;
	bool				hasNick() const;
	bool				hasUser() const;
	time_t				getLastActivity() const;
	bool				isPingSent() const;
	bool				isPendingClose() const;
	/* Seconds since the epoch, sub-second precision -- lets the
	** pending-close deadline (Server::_pendingCloseTimeoutSec) be tuned
	** below one second, e.g. by tests. */
	double				getPendingCloseSince() const;
	bool				isTearingDown() const;
	std::string			getPrefix() const;

	/* ─── Setters ─── */
	void	setNickname(const std::string &nickname);
	void	setUsername(const std::string &username);
	void	setRealname(const std::string &realname);
	void	setPassword(const std::string &password);
	void	setAuthenticated(bool auth);
	void	setRegistered(bool reg);
	void	setPassSent(bool sent);
	void	setNickSet(bool set);
	void	setUserSet(bool set);
	void	updateLastActivity();
	void	setPingSent(bool sent);
	void	markPendingClose();
	/* Self-guarding latch: true from the first instant teardown starts
	** (before the drain-vs-finalize decision is even made) so a callback
	** re-entering disconnect logic for this same client mid-teardown can
	** be recognized and turned into a no-op. See Server::teardownClientState. */
	void	markTearingDown();

	/* ─── Buffer management ─── */
	void						appendToRecvBuffer(const std::string &data);
	std::vector<std::string>	extractMessages();
	void						queueMessage(const std::string &msg);
	const std::string			&getSendBuffer() const;
	void						clearSendBuffer(size_t bytesSent);
	bool						hasPendingData() const;
	bool						isSendQExceeded() const;

private:
	Client();
	Client(const Client &other);
	Client &operator=(const Client &other);

	int			_fd;
	std::string	_nickname;
	std::string	_username;
	std::string	_realname;
	std::string	_hostname;
	std::string	_password;

	bool		_authenticated;
	bool		_registered;
	bool		_passSent;
	bool		_nickSet;
	bool		_userSet;

	libcpp98::BufferedSocket	_io;

	time_t		_lastActivity;
	bool		_pingSent;

	bool		_pendingClose;
	double		_pendingCloseSince;
	bool		_tearingDown;
};

#endif
