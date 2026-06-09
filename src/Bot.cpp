/* ─── Bot: internal IRC bot (bonus) ─── */

#include "Bot.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "libcpp/str/format.hpp"

#include <ctime>
#include <cstdlib>

const char *Bot::_jokes[] = {
	"Why do programmers prefer dark mode? Because light attracts bugs.",
	"There are only 10 types of people in the world: those who understand binary and those who don't.",
	"A SQL query walks into a bar, sees two tables and asks: 'Can I JOIN you?'",
	"Why do Java programmers wear glasses? Because they don't C#.",
	"!false — It's funny because it's true.",
	"How many programmers does it take to change a light bulb? None, that's a hardware problem.",
	"An IRC user walks into a bar. The bartender says: 'What'll it be?' The user says: '/quit'.",
	"Knock knock. Who's there? Recursion. Recursion who? Knock knock."
};

const int Bot::_jokeCount = 8;

Bot::Bot(Server *server)
	: _server(server),
	  _nickname("ircbot")
{
}

Bot::~Bot() {}

const std::string &Bot::getNickname() const
{
	return _nickname;
}

void Bot::handleMessage(Client *sender, const std::string &text)
{
	if (text.empty())
		return;

	// Parse command
	std::string cmd;
	std::string param;

	std::istringstream iss(text);
	iss >> cmd;
	if (iss)
		std::getline(iss >> std::ws, param);

	if (cmd == "!help")
		cmdHelp(sender);
	else if (cmd == "!time")
		cmdTime(sender);
	else if (cmd == "!info")
		cmdInfo(sender, param);
	else if (cmd == "!joke")
		cmdJoke(sender);
	else
		reply(sender, "Unknown command. Type !help for available commands.");
}

void Bot::cmdHelp(Client *sender)
{
	reply(sender, "Available commands:");
	reply(sender, "  !help           - Show this help message");
	reply(sender, "  !time           - Show current server time");
	reply(sender, "  !info [#chan]    - Show server or channel info");
	reply(sender, "  !joke           - Tell a random joke");
}

void Bot::cmdTime(Client *sender)
{
	time_t now = std::time(NULL);
	char buf[64];
	struct tm *tm = std::localtime(&now);
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
	reply(sender, std::string("Server time: ") + buf);
}

void Bot::cmdInfo(Client *sender, const std::string &param)
{
	if (param.empty() || param[0] != '#')
	{
		// Server info
		reply(sender, "Server: " + _server->getServerName()
				  + " v" + SERVER_VERSION);
		return;
	}

	// Channel info
	Channel *chan = _server->findChannel(param);
	if (!chan)
	{
		reply(sender, "Channel " + param + " does not exist.");
		return;
	}

	reply(sender, "Channel " + param + ": "
			  + libcpp::str::to_string(chan->getMemberCount())
			  + " users, modes: " + chan->getModeString());

	if (!chan->getTopic().empty())
		reply(sender, "Topic: " + chan->getTopic());
}

void Bot::cmdJoke(Client *sender)
{
	std::srand(static_cast<unsigned int>(std::time(NULL)));
	int idx = std::rand() % _jokeCount;
	reply(sender, _jokes[idx]);
}

void Bot::reply(Client *sender, const std::string &text)
{
	sender->queueMessage(":" + _nickname + " PRIVMSG "
						 + sender->getNickname() + " :" + text);
}
