#include "Client.hpp"
#include "Replies.hpp"
#include <cstring>

Client::Client(int fd, const std::string &hostname)
	: _fd(fd),
	  _nickname("*"),
	  _hostname(hostname),
	  _authenticated(false),
	  _registered(false),
	  _passSent(false),
	  _nickSet(false),
	  _userSet(false),
	  _lastActivity(std::time(NULL)),
	  _pingSent(false)
{
}

Client::~Client() {}

/* ─── Getters ─── */

int					Client::getFd() const { return _fd; }
const std::string	&Client::getNickname() const { return _nickname; }
const std::string	&Client::getUsername() const { return _username; }
const std::string	&Client::getRealname() const { return _realname; }
const std::string	&Client::getHostname() const { return _hostname; }
const std::string	&Client::getPassword() const { return _password; }
bool				Client::isAuthenticated() const { return _authenticated; }
bool				Client::isRegistered() const { return _registered; }
bool				Client::hasPassSent() const { return _passSent; }
bool				Client::hasNick() const { return _nickSet; }
bool				Client::hasUser() const { return _userSet; }
time_t				Client::getLastActivity() const { return _lastActivity; }
bool				Client::isPingSent() const { return _pingSent; }

std::string Client::getPrefix() const
{
	return _nickname + "!" + _username + "@" + _hostname;
}

/* ─── Setters ─── */

void	Client::setNickname(const std::string &nickname) { _nickname = nickname; }
void	Client::setUsername(const std::string &username) { _username = username; }
void	Client::setRealname(const std::string &realname) { _realname = realname; }
void	Client::setPassword(const std::string &password) { _password = password; }
void	Client::setAuthenticated(bool auth) { _authenticated = auth; }
void	Client::setRegistered(bool reg) { _registered = reg; }
void	Client::setPassSent(bool sent) { _passSent = sent; }
void	Client::setNickSet(bool set) { _nickSet = set; }
void	Client::setUserSet(bool set) { _userSet = set; }
void	Client::updateLastActivity() { _lastActivity = std::time(NULL); }
void	Client::setPingSent(bool sent) { _pingSent = sent; }

/* ─── Buffer management ─── */

void Client::appendToRecvBuffer(const std::string &data)
{
	_recvBuffer += data;
}

std::vector<std::string> Client::extractMessages()
{
	std::vector<std::string> messages;
	std::string::size_type pos;

	while ((pos = _recvBuffer.find("\r\n")) != std::string::npos)
	{
		std::string line = _recvBuffer.substr(0, pos);
		_recvBuffer.erase(0, pos + 2);
		if (!line.empty())
			messages.push_back(line);
	}
	// Also handle bare \n (some clients / nc may send it)
	while ((pos = _recvBuffer.find('\n')) != std::string::npos)
	{
		std::string line = _recvBuffer.substr(0, pos);
		_recvBuffer.erase(0, pos + 1);
		// Remove trailing \r if present
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (!line.empty())
			messages.push_back(line);
	}
	// Safety: prevent buffer from growing indefinitely without newline
	if (_recvBuffer.size() > MAX_MSGLEN)
	{
		messages.push_back(_recvBuffer.substr(0, MAX_MSGLEN));
		_recvBuffer.clear();
	}
	return messages;
}

void Client::queueMessage(const std::string &msg)
{
	_sendBuffer += msg + "\r\n";
}

const std::string &Client::getSendBuffer() const
{
	return _sendBuffer;
}

void Client::clearSendBuffer(size_t bytesSent)
{
	_sendBuffer.erase(0, bytesSent);
}

bool Client::hasPendingData() const
{
	return !_sendBuffer.empty();
}
