/* ─── Registration commands: CAP, PASS, NICK, USER ─── */

#include "Server.hpp"
#include "Bot.hpp"
#include "libcpp/str/case.hpp"

#include <iostream>

/* ─── CAP ─── */

void Server::cmdCap(Client *client, const Message &msg)
{
	if (msg.params.empty())
		return;

	std::string subcommand = libcpp::str::to_upper(msg.params[0]);

	if (subcommand == "LS")
	{
		// No capabilities supported — send empty list
		client->queueMessage(":" + _serverName + " CAP * LS :");
	}
	else if (subcommand == "END")
	{
		// Nothing to do — registration proceeds normally
	}
	// Ignore other CAP subcommands (REQ, etc.)
}

/* ─── PASS ─── */

void Server::cmdPass(Client *client, const Message &msg)
{
	if (client->isRegistered())
	{
		sendReply(client, ERR_ALREADYREGISTRED,
				  ":You may not reregister");
		return;
	}
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "PASS :Not enough parameters");
		return;
	}
	client->setPassword(msg.params[0]);
	client->setPassSent(true);
}

/* ─── NICK ─── */

void Server::cmdNick(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NONICKNAMEGIVEN,
				  ":No nickname given");
		return;
	}

	std::string nick = msg.params[0];

	if (!isValidNickname(nick))
	{
		sendReply(client, ERR_ERRONEUSNICKNAME,
				  nick + " :Erroneous nickname");
		return;
	}

	// Check if nickname is taken (case-sensitive for simplicity)
	Client *existing = findClientByNick(nick);
	if (existing && existing != client)
	{
		sendReply(client, ERR_NICKNAMEINUSE,
				  nick + " :Nickname is already in use");
		return;
	}

	// Reject if it collides with bot nickname
	if (_bot && nick == _bot->getNickname())
	{
		sendReply(client, ERR_NICKNAMEINUSE,
				  nick + " :Nickname is already in use");
		return;
	}

	if (client->isRegistered())
	{
		// Nick change — broadcast to all shared channels
		std::string oldPrefix = client->getPrefix();
		std::string nickMsg = ":" + oldPrefix + " NICK :" + nick;

		// Send to the client itself
		client->queueMessage(nickMsg);
		// Send to all users sharing a channel
		broadcastToChannels(client, nickMsg);

		client->setNickname(nick);
	}
	else
	{
		client->setNickname(nick);
		client->setNickSet(true);

		// Check if registration can be completed
		if (client->hasUser())
			completeRegistration(client);
	}
}

/* ─── USER ─── */

void Server::cmdUser(Client *client, const Message &msg)
{
	if (client->isRegistered())
	{
		sendReply(client, ERR_ALREADYREGISTRED,
				  ":You may not reregister");
		return;
	}
	if (msg.params.size() < 4)
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "USER :Not enough parameters");
		return;
	}

	client->setUsername(msg.params[0]);
	// params[1] = mode (ignored), params[2] = unused
	client->setRealname(msg.params[3]);
	client->setUserSet(true);

	if (client->hasNick())
		completeRegistration(client);
}

/* ─── Complete Registration ─── */

void Server::completeRegistration(Client *client)
{
	// Check password
	if (!client->hasPassSent() || client->getPassword() != _password)
	{
		sendReply(client, ERR_PASSWDMISMATCH,
				  ":Password incorrect");
		disconnectClient(client->getFd(), "Password mismatch");
		return;
	}

	client->setAuthenticated(true);
	client->setRegistered(true);

	std::string nick = client->getNickname();
	std::string prefix = client->getPrefix();

	// 001 RPL_WELCOME
	sendReply(client, RPL_WELCOME,
			  ":Welcome to the " + _serverName + " Network " + prefix);

	// 002 RPL_YOURHOST
	sendReply(client, RPL_YOURHOST,
			  ":Your host is " + _serverName + ", running version "
			  + SERVER_VERSION);

	// 003 RPL_CREATED
	sendReply(client, RPL_CREATED,
			  ":This server was created " + std::string(SERVER_CREATED));

	// 004 RPL_MYINFO
	sendReply(client, RPL_MYINFO,
			  _serverName + " " + SERVER_VERSION + " o itkol");

	// 005 RPL_ISUPPORT
	sendReply(client, RPL_ISUPPORT,
			  "CHANTYPES=# PREFIX=(o)@ CHANMODES=,k,,itl "
			  "NICKLEN=9 CHANNELLEN=50 TOPICLEN=390 "
			  "NETWORK=" + _serverName + " CASEMAPPING=ascii "
			  ":are supported by this server");

	// 422 ERR_NOMOTD (we don't have a MOTD file)
	sendReply(client, ERR_NOMOTD,
			  ":MOTD File is missing");

	std::cout << "Client registered: " << nick << " ("
			  << client->getUsername() << "@" << client->getHostname()
			  << ")" << std::endl;
}
