#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <vector>
# include <ctime>

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

	/* ─── Buffer management ─── */
	void						appendToRecvBuffer(const std::string &data);
	std::vector<std::string>	extractMessages();
	void						queueMessage(const std::string &msg);
	const std::string			&getSendBuffer() const;
	void						clearSendBuffer(size_t bytesSent);
	bool						hasPendingData() const;

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

	std::string	_recvBuffer;
	std::string	_sendBuffer;

	time_t		_lastActivity;
	bool		_pingSent;
};

#endif
