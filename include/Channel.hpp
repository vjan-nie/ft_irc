#ifndef CHANNEL_HPP
# define CHANNEL_HPP

# include <string>
# include <map>
# include <set>
# include <vector>
# include <ctime>

class Client;

class Channel
{
public:
	Channel(const std::string &name, Client *creator);
	~Channel();

	/* ─── Getters ─── */
	const std::string	&getName() const;
	const std::string	&getTopic() const;
	const std::string	&getTopicSetter() const;
	time_t				getTopicTime() const;
	time_t				getCreationTime() const;
	const std::string	&getKey() const;
	size_t				getUserLimit() const;
	bool				isInviteOnly() const;
	bool				isTopicRestricted() const;
	size_t				getMemberCount() const;
	std::string			getModeString() const;
	std::string			getModeParams() const;
	std::string			getNamesList() const;

	/* ─── Setters ─── */
	void	setTopic(const std::string &topic, const std::string &setter);
	void	setKey(const std::string &key);
	void	removeKey();
	void	setUserLimit(size_t limit);
	void	removeUserLimit();
	void	setInviteOnly(bool inviteOnly);
	void	setTopicRestricted(bool restricted);

	/* ─── Member management ─── */
	void	addMember(Client *client);
	void	removeMember(Client *client);
	bool	isMember(Client *client) const;
	bool	isMember(const std::string &nickname) const;
	bool	isEmpty() const;

	/* ─── Operator management ─── */
	bool	isOperator(Client *client) const;
	void	setOperator(Client *client, bool op);

	/* ─── Invite management ─── */
	void	addInvite(const std::string &nickname);
	bool	isInvited(const std::string &nickname) const;
	void	removeInvite(const std::string &nickname);

	/* ─── Messaging ─── */
	void	broadcastMessage(const std::string &msg, Client *exclude);

	/* ─── Utility ─── */
	Client							*findMember(const std::string &nickname) const;
	std::vector<Client *>			getMembers() const;

private:
	Channel();
	Channel(const Channel &other);
	Channel &operator=(const Channel &other);

	std::string				_name;
	std::string				_topic;
	std::string				_topicSetter;
	time_t					_topicTime;
	time_t					_creationTime;
	std::string				_key;
	size_t					_userLimit;
	bool					_inviteOnly;
	bool					_topicRestricted;

	std::map<int, Client *>	_members;
	std::set<int>			_operators;
	std::set<std::string>	_inviteList;
};

#endif
