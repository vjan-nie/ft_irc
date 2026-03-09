/* ─── Channel commands: JOIN, PART ─── */

#include "Server.hpp"

#include <sstream>
#include <iostream>

/* ─── JOIN ─── */

void Server::cmdJoin(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "JOIN :Not enough parameters");
		return;
	}

	// Parse channel names and keys
	std::istringstream chanStream(msg.params[0]);
	std::string chanName;
	std::vector<std::string> channels;
	while (std::getline(chanStream, chanName, ','))
	{
		if (!chanName.empty())
			channels.push_back(chanName);
	}

	std::vector<std::string> keys;
	if (msg.params.size() > 1)
	{
		std::istringstream keyStream(msg.params[1]);
		std::string key;
		while (std::getline(keyStream, key, ','))
			keys.push_back(key);
	}

	for (size_t i = 0; i < channels.size(); ++i)
	{
		const std::string &name = channels[i];
		std::string key = (i < keys.size()) ? keys[i] : "";

		if (!isValidChannelName(name))
		{
			sendReply(client, ERR_BADCHANMASK,
					  name + " :Bad Channel Mask");
			continue;
		}

		Channel *chan = findChannel(name);
		if (chan)
		{
			// Channel exists — check restrictions
			if (chan->isMember(client))
				continue; // Already in channel

			// Check invite-only
			if (chan->isInviteOnly() && !chan->isInvited(client->getNickname()))
			{
				sendReply(client, ERR_INVITEONLYCHAN,
						  name + " :Cannot join channel (+i)");
				continue;
			}

			// Check channel key
			if (!chan->getKey().empty() && chan->getKey() != key)
			{
				sendReply(client, ERR_BADCHANNELKEY,
						  name + " :Cannot join channel (+k)");
				continue;
			}

			// Check user limit
			if (chan->getUserLimit() > 0
				&& chan->getMemberCount() >= chan->getUserLimit())
			{
				sendReply(client, ERR_CHANNELISFULL,
						  name + " :Cannot join channel (+l)");
				continue;
			}

			chan->addMember(client);
			chan->removeInvite(client->getNickname());
		}
		else
		{
			// Create new channel — creator becomes operator
			chan = createChannel(name, client);
			if (!chan)
			{
				sendReply(client, ERR_NOSUCHCHANNEL,
						  name + " :Cannot create channel (server error)");
				continue;
			}
		}

		// Broadcast JOIN to all channel members (including the joiner)
		chan->broadcastMessage(":" + client->getPrefix()
							  + " JOIN " + name, NULL);

		// Send topic
		if (!chan->getTopic().empty())
		{
			sendReply(client, RPL_TOPIC,
					  name + " :" + chan->getTopic());
			std::ostringstream oss;
			oss << chan->getTopicTime();
			sendReply(client, RPL_TOPICWHOTIME,
					  name + " " + chan->getTopicSetter() + " "
					  + oss.str());
		}
		else
		{
			sendReply(client, RPL_NOTOPIC,
					  name + " :No topic is set");
		}

		// Send names list
		sendReply(client, RPL_NAMREPLY,
				  "= " + name + " :" + chan->getNamesList());
		sendReply(client, RPL_ENDOFNAMES,
				  name + " :End of /NAMES list");
	}
}

/* ─── PART ─── */

void Server::cmdPart(Client *client, const Message &msg)
{
	if (msg.params.empty())
	{
		sendReply(client, ERR_NEEDMOREPARAMS,
				  "PART :Not enough parameters");
		return;
	}

	std::string reason;
	if (msg.params.size() > 1)
		reason = msg.params[1];

	std::istringstream chanStream(msg.params[0]);
	std::string chanName;

	while (std::getline(chanStream, chanName, ','))
	{
		if (chanName.empty())
			continue;

		Channel *chan = findChannel(chanName);
		if (!chan)
		{
			sendReply(client, ERR_NOSUCHCHANNEL,
					  chanName + " :No such channel");
			continue;
		}

		if (!chan->isMember(client))
		{
			sendReply(client, ERR_NOTONCHANNEL,
					  chanName + " :You're not on that channel");
			continue;
		}

		std::string partMsg = ":" + client->getPrefix()
							  + " PART " + chanName;
		if (!reason.empty())
			partMsg += " :" + reason;

		chan->broadcastMessage(partMsg, NULL);
		chan->removeMember(client);

		if (chan->isEmpty())
			removeChannel(chanName);
	}
}
