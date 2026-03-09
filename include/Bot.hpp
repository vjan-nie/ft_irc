#ifndef BOT_HPP
# define BOT_HPP

# include <string>
# include <vector>

class Server;
class Client;
class Message;

class Bot
{
public:
	Bot(Server *server);
	~Bot();

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
