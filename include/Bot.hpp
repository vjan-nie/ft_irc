#ifndef BOT_HPP
# define BOT_HPP

# include <string>
# include <vector>

# include "ext/IServerExtension.hpp"

class Server;
class Client;
struct Message;

/* Bonus bot — a virtual participant plugged in through the extension seam:
** it claims PRIVMSGs addressed to its nick (onPrivmsg) and reserves that
** nick against clients (reservesNick). */
class Bot : public IServerExtension
{
public:
	Bot(Server *server);
	~Bot();

	/* ─── IServerExtension ─── */
	const char	*name() const;
	bool		onPrivmsg(Server &server, Client &sender,
						  const std::string &target, const std::string &text);
	bool		reservesNick(const std::string &nick) const;

	const std::string	&getNickname() const;
	void				handleMessage(Client *sender, const std::string &text);

private:
	Bot();
	Bot(const Bot &other);
	Bot &operator=(const Bot &other);

	void	cmdHelp(Client *sender);
	void	cmdTime(Client *sender);
	void	cmdInfo(Client *sender, const std::string &param);
	void	cmdJoke(Client *sender);

	void	reply(Client *sender, const std::string &text);

	Server		*_server;
	std::string	_nickname;

	static const char *_jokes[];
	static const int _jokeCount;
};

#endif
