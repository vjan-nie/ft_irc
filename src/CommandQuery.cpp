/* ─── Query commands: WHO, WHOIS, USERHOST ─── */

#include "Server.hpp"

#include <sstream>

/* ─── WHO ─── */

void Server::cmdWho(Client *client, const Message &msg)
{
	if (msg.params.empty())
		return;

	const std::string &target = msg.params[0];

	if (target[0] == '#')
	{
		// WHO #channel
		Channel *chan = findChannel(target);
		if (!chan)
		{
			sendReply(client, RPL_ENDOFWHO,
					  target + " :End of WHO list");
			return;
		}

		std::vector<Client *> members = chan->getMembers();
		for (size_t i = 0; i < members.size(); ++i)
		{
			Client *m = members[i];
			std::string flags = "H";
			if (chan->isOperator(m))
				flags += "@";

			// :server 352 requester channel user host server nick flags :0 realname
			sendReply(client, RPL_WHOREPLY,
					  target + " " + m->getUsername() + " "
					  + m->getHostname() + " " + _serverName + " "
					  + m->getNickname() + " " + flags
					  + " :0 " + m->getRealname());
		}
		sendReply(client, RPL_ENDOFWHO,
				  target + " :End of WHO list");
	}
	else
	{
		// WHO nickname
		Client *dest = findClientByNick(target);
		if (dest)
		{
			sendReply(client, RPL_WHOREPLY,
					  "* " + dest->getUsername() + " "
					  + dest->getHostname() + " " + _serverName + " "
					  + dest->getNickname() + " H"
					  + " :0 " + dest->getRealname());
		}
		sendReply(client, RPL_ENDOFWHO,
				  target + " :End of WHO list");
	}
}

/* ─── WHOIS ─── */

void Server::cmdWhois(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NONICKNAMEGIVEN,
				  ":No nickname given");
		return;
	}

	const std::string &nick = msg.params[msg.params.size() > 1 ? 1 : 0];

	Client *dest = findClientByNick(nick);
	if (!dest)
	{
		sendReply(client, ERR_NOSUCHNICK,
				  nick + " :No such nick/channel");
		return;
	}

	// 311 RPL_WHOISUSER
	sendReply(client, RPL_WHOISUSER,
			  dest->getNickname() + " " + dest->getUsername() + " "
			  + dest->getHostname() + " * :" + dest->getRealname());

	// 312 RPL_WHOISSERVER
	sendReply(client, RPL_WHOISSERVER,
			  dest->getNickname() + " " + _serverName + " :ft_irc server");

	// 319 RPL_WHOISCHANNELS — list channels the user is on
	std::string chanList;
	for (std::map<std::string, Channel *>::const_iterator it = _channels.begin();
		 it != _channels.end(); ++it)
	{
		if (it->second->isMember(dest))
		{
			if (!chanList.empty())
				chanList += " ";
			if (it->second->isOperator(dest))
				chanList += "@";
			chanList += it->first;
		}
	}
	if (!chanList.empty())
	{
		sendReply(client, RPL_WHOISCHANNELS,
				  dest->getNickname() + " :" + chanList);
	}

	// 318 RPL_ENDOFWHOIS
	sendReply(client, RPL_ENDOFWHOIS,
			  dest->getNickname() + " :End of WHOIS list");
}

/* ─── USERHOST ─── */

void Server::cmdUserhost(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "USERHOST :Not enough parameters");
		return;
	}

	std::string reply;
	for (size_t i = 0; i < msg.params.size() && i < 5; ++i)
	{
		Client *dest = findClientByNick(msg.params[i]);
		if (dest)
		{
			if (!reply.empty())
				reply += " ";
			reply += dest->getNickname() + "=+"
					 + dest->getUsername() + "@" + dest->getHostname();
		}
	}

	sendReply(client, RPL_USERHOST, ":" + reply);
}
