#include "Channel.hpp"
#include "Client.hpp"
#include <sstream>

Channel::Channel(const std::string &name, Client *creator)
	: _name(name),
	  _topicTime(0),
	  _creationTime(std::time(NULL)),
	  _userLimit(0),
	  _inviteOnly(false),
	  _topicRestricted(false)
{
	addMember(creator);
	setOperator(creator, true);
}

Channel::~Channel() {}

/* ─── Getters ─── */

const std::string	&Channel::getName() const { return _name; }
const std::string	&Channel::getTopic() const { return _topic; }
const std::string	&Channel::getTopicSetter() const { return _topicSetter; }
time_t				Channel::getTopicTime() const { return _topicTime; }
time_t				Channel::getCreationTime() const { return _creationTime; }
const std::string	&Channel::getKey() const { return _key; }
size_t				Channel::getUserLimit() const { return _userLimit; }
bool				Channel::isInviteOnly() const { return _inviteOnly; }
bool				Channel::isTopicRestricted() const { return _topicRestricted; }
size_t				Channel::getMemberCount() const { return _members.size(); }

std::string Channel::getModeString() const
{
	std::string modes = "+";
	if (_inviteOnly)
		modes += "i";
	if (_topicRestricted)
		modes += "t";
	if (!_key.empty())
		modes += "k";
	if (_userLimit > 0)
		modes += "l";
	if (modes == "+")
		return "+";
	return modes;
}

std::string Channel::getModeParams() const
{
	std::string params;
	if (!_key.empty())
	{
		if (!params.empty())
			params += " ";
		params += _key;
	}
	if (_userLimit > 0)
	{
		if (!params.empty())
			params += " ";
		std::ostringstream oss;
		oss << _userLimit;
		params += oss.str();
	}
	return params;
}

std::string Channel::getNamesList() const
{
	std::string names;
	for (std::map<int, Client *>::const_iterator it = _members.begin();
		 it != _members.end(); ++it)
	{
		if (!names.empty())
			names += " ";
		if (_operators.count(it->first))
			names += "@";
		names += it->second->getNickname();
	}
	return names;
}

/* ─── Setters ─── */

void Channel::setTopic(const std::string &topic, const std::string &setter)
{
	_topic = topic;
	_topicSetter = setter;
	_topicTime = std::time(NULL);
}

void	Channel::setKey(const std::string &key) { _key = key; }
void	Channel::removeKey() { _key.clear(); }

void	Channel::setUserLimit(size_t limit) { _userLimit = limit; }
void	Channel::removeUserLimit() { _userLimit = 0; }

void	Channel::setInviteOnly(bool inviteOnly) { _inviteOnly = inviteOnly; }
void	Channel::setTopicRestricted(bool restricted) { _topicRestricted = restricted; }

/* ─── Member management ─── */

void Channel::addMember(Client *client)
{
	_members[client->getFd()] = client;
}

void Channel::removeMember(Client *client)
{
	_members.erase(client->getFd());
	_operators.erase(client->getFd());
}

bool Channel::isMember(Client *client) const
{
	return _members.count(client->getFd()) > 0;
}

bool Channel::isMember(const std::string &nickname) const
{
	for (std::map<int, Client *>::const_iterator it = _members.begin();
		 it != _members.end(); ++it)
	{
		if (it->second->getNickname() == nickname)
			return true;
	}
	return false;
}

bool Channel::isEmpty() const
{
	return _members.empty();
}

/* ─── Operator management ─── */

bool Channel::isOperator(Client *client) const
{
	return _operators.count(client->getFd()) > 0;
}

void Channel::setOperator(Client *client, bool op)
{
	if (op)
		_operators.insert(client->getFd());
	else
		_operators.erase(client->getFd());
}

/* ─── Invite management ─── */

void Channel::addInvite(const std::string &nickname)
{
	_inviteList.insert(nickname);
}

bool Channel::isInvited(const std::string &nickname) const
{
	return _inviteList.count(nickname) > 0;
}

void Channel::removeInvite(const std::string &nickname)
{
	_inviteList.erase(nickname);
}

/* ─── Messaging ─── */

void Channel::broadcastMessage(const std::string &msg, Client *exclude)
{
	for (std::map<int, Client *>::iterator it = _members.begin();
		 it != _members.end(); ++it)
	{
		if (exclude && it->second == exclude)
			continue;
		it->second->queueMessage(msg);
	}
}

/* ─── Utility ─── */

Client *Channel::findMember(const std::string &nickname) const
{
	for (std::map<int, Client *>::const_iterator it = _members.begin();
		 it != _members.end(); ++it)
	{
		if (it->second->getNickname() == nickname)
			return it->second;
	}
	return NULL;
}

std::vector<Client *> Channel::getMembers() const
{
	std::vector<Client *> result;
	for (std::map<int, Client *>::const_iterator it = _members.begin();
		 it != _members.end(); ++it)
	{
		result.push_back(it->second);
	}
	return result;
}
