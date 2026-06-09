/* ─── Messaging commands: PRIVMSG, NOTICE, PING, PONG, QUIT ─── */

#include "Server.hpp"
#include "Bot.hpp"
#include "IrcCase.hpp"

#include <sstream>
#include <iostream>

/* ─── PRIVMSG ─── */

void Server::cmdPrivmsg(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NORECIPIENT,
				  ":No recipient given (PRIVMSG)");
		return;
	}
	if (msg.params.size() < 2)
	{
		sendReply(client, ERR_NOTEXTTOSEND,
				  ":No text to send");
		return;
	}

	std::string targets = msg.params[0];
	const std::string &text = msg.params[1];

	// Split comma-separated targets
	std::istringstream iss(targets);
	std::string target;
	while (std::getline(iss, target, ','))
	{
		if (target.empty())
			continue;

		if (target[0] == '#')
		{
			// Channel message
			Channel *chan = findChannel(target);
			if (!chan)
			{
				sendReply(client, ERR_NOSUCHCHANNEL,
						  target + " :No such channel");
				continue;
			}
			if (!chan->isMember(client))
			{
				sendReply(client, ERR_CANNOTSENDTOCHAN,
						  target + " :Cannot send to channel");
				continue;
			}
			chan->broadcastMessage(":" + client->getPrefix()
								  + " PRIVMSG " + target + " :" + text,
								  client);
		}
		else
		{
			// Check if it's the bot
			if (_bot && ircEquals(target, _bot->getNickname()))
			{
				_bot->handleMessage(client, text);
				continue;
			}

			// Private message to user
			Client *dest = findClientByNick(target);
			if (!dest)
			{
				sendReply(client, ERR_NOSUCHNICK,
						  target + " :No such nick/channel");
				continue;
			}
			dest->queueMessage(":" + client->getPrefix()
							   + " PRIVMSG " + target + " :" + text);
		}
	}
}

/* ─── NOTICE ─── */

void Server::cmdNotice(Client *client, const Message &msg)
{
	// NOTICE is like PRIVMSG but must never generate automatic replies
	if (msg.params.size() < 2)
		return;

	std::string targets = msg.params[0];
	const std::string &text = msg.params[1];

	std::istringstream iss(targets);
	std::string target;
	while (std::getline(iss, target, ','))
	{
		if (target.empty())
			continue;

		if (target[0] == '#')
		{
			Channel *chan = findChannel(target);
			if (!chan || !chan->isMember(client))
				continue;
			chan->broadcastMessage(":" + client->getPrefix()
								  + " NOTICE " + target + " :" + text,
								  client);
		}
		else
		{
			Client *dest = findClientByNick(target);
			if (!dest)
				continue;
			dest->queueMessage(":" + client->getPrefix()
							   + " NOTICE " + target + " :" + text);
		}
	}
}

/* ─── PING ─── */

void Server::cmdPing(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "PING :Not enough parameters");
		return;
	}
	client->queueMessage(":" + _serverName + " PONG " + _serverName
						 + " :" + msg.params[0]);
}

/* ─── PONG ─── */

void Server::cmdPong(Client *client, const Message &msg)
{
	(void)msg;
	client->updateLastActivity();
	client->setPingSent(false);
}

/* ─── QUIT ─── */

void Server::cmdQuit(Client *client, const Message &msg)
{
	std::string reason = "Quit";
	if (!msg.params.empty())
		reason = msg.params[0];

	int fd = client->getFd();
	disconnectClient(fd, reason);
}
