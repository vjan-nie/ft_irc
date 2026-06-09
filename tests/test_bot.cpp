/* ─── Unit tests: Bot commands ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "Bot.hpp"
#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"

/* ══════════════════════════════════════════════════════════════════
 * Fixture: spins up a real Server on a free port so the Bot can
 * call getServerName() / findChannel().  We don't connect any
 * real sockets — we just exercise Bot::handleMessage().
 * ══════════════════════════════════════════════════════════════ */

class BotTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		/* Pick a high port unlikely to conflict */
		server = NULL;
		for (int port = 16700; port < 16800; ++port)
		{
			try
			{
				server = new Server(port, "bottest");
				testPort = port;
				break;
			}
			catch (...)
			{
				continue;
			}
		}
		ASSERT_NE(server, nullptr) << "Could not bind to any port for BotTest";
		/* The bot is an extension now; build our own instance against the
		 * live server and let the fixture own it. */
		bot = new Bot(server);
		ASSERT_NE(bot, nullptr);

		sender = new Client(999, "127.0.0.1");
		sender->setNickname("tester");
		sender->setUsername("tuser");
	}

	void TearDown() override
	{
		delete sender;
		delete bot;
		delete server;
	}

	std::string getReply()
	{
		std::string buf = sender->getSendBuffer();
		sender->clearSendBuffer(buf.size());
		return buf;
	}

	Server *server;
	Bot *bot;
	Client *sender;
	int testPort;
};

/* ════════════════════════════════════════════════════════════════════════
 * Suite: BotCommands
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(BotTest, Nickname)
{
	EXPECT_EQ(bot->getNickname(), "ircbot");
}

TEST_F(BotTest, HelpCommand)
{
	bot->handleMessage(sender, "!help");
	std::string reply = getReply();
	EXPECT_NE(reply.find("Available commands"), std::string::npos);
	EXPECT_NE(reply.find("!help"), std::string::npos);
	EXPECT_NE(reply.find("!time"), std::string::npos);
	EXPECT_NE(reply.find("!info"), std::string::npos);
	EXPECT_NE(reply.find("!joke"), std::string::npos);
}

TEST_F(BotTest, TimeCommand)
{
	bot->handleMessage(sender, "!time");
	std::string reply = getReply();
	EXPECT_NE(reply.find("Server time:"), std::string::npos);
}

TEST_F(BotTest, InfoCommandServer)
{
	bot->handleMessage(sender, "!info");
	std::string reply = getReply();
	EXPECT_NE(reply.find("Server:"), std::string::npos);
	EXPECT_NE(reply.find("ft_irc"), std::string::npos);
}

TEST_F(BotTest, InfoCommandChannelNotFound)
{
	bot->handleMessage(sender, "!info #nonexistent");
	std::string reply = getReply();
	EXPECT_NE(reply.find("does not exist"), std::string::npos);
}

TEST_F(BotTest, JokeCommand)
{
	bot->handleMessage(sender, "!joke");
	std::string reply = getReply();
	/* Should contain PRIVMSG from ircbot */
	EXPECT_NE(reply.find(":ircbot PRIVMSG tester :"), std::string::npos);
	/* Non-trivial content */
	EXPECT_GT(reply.size(), 30u);
}

TEST_F(BotTest, UnknownCommand)
{
	bot->handleMessage(sender, "!invalid");
	std::string reply = getReply();
	EXPECT_NE(reply.find("Unknown command"), std::string::npos);
}

TEST_F(BotTest, EmptyMessage)
{
	bot->handleMessage(sender, "");
	std::string reply = getReply();
	/* Empty message should produce no output */
	EXPECT_TRUE(reply.empty());
}

TEST_F(BotTest, CommandWithExtraSpace)
{
	bot->handleMessage(sender, "!help   extra");
	std::string reply = getReply();
	EXPECT_NE(reply.find("Available commands"), std::string::npos);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: BotMemory — PostMan leak detection
 * ════════════════════════════════════════════════════════════════════ */

extern int g_allocations;

TEST_F(BotTest, NoLeakOnMessages)
{
	int before = g_allocations;

	bot->handleMessage(sender, "!help");
	sender->clearSendBuffer(sender->getSendBuffer().size());
	bot->handleMessage(sender, "!time");
	sender->clearSendBuffer(sender->getSendBuffer().size());
	bot->handleMessage(sender, "!joke");
	sender->clearSendBuffer(sender->getSendBuffer().size());

	EXPECT_LE(g_allocations, before + 3)
		<< "Bot messages should not accumulate leaks";
}
