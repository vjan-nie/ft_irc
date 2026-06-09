/* ─── Integration tests: full IRC protocol via TCP sockets ─── */

#include "PostMan.hpp"
#include "TestHarness.hpp"

/* Fixture: server in a background thread (see TestHarness.hpp) */
class IntegrationTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17100; }
};

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Registration
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, SuccessfulRegistration)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("PASS testpass");
	tc.sendCmd("NICK integ1");
	tc.sendCmd("USER integ1 0 * :Integration Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "001")) << "Expected RPL_WELCOME";
	EXPECT_TRUE(tc.hasNumeric(reply, "002")) << "Expected RPL_YOURHOST";
	EXPECT_TRUE(tc.hasNumeric(reply, "003")) << "Expected RPL_CREATED";
	EXPECT_TRUE(tc.hasNumeric(reply, "004")) << "Expected RPL_MYINFO";
	EXPECT_TRUE(tc.hasNumeric(reply, "005")) << "Expected RPL_ISUPPORT";
	EXPECT_TRUE(tc.hasNumeric(reply, "422")) << "Expected ERR_NOMOTD";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, WrongPassword)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("PASS wrongpass");
	tc.sendCmd("NICK wrongpw");
	tc.sendCmd("USER wrongpw 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	/* Server queues 464 but may disconnect before flushing.
	   Either we see 464 or the connection was simply closed. */
	EXPECT_TRUE(reply.empty() || tc.hasNumeric(reply, "464"))
		<< "Expected either ERR_PASSWDMISMATCH or closed connection";
}

TEST_F(IntegrationTest, NoPassword)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("NICK nopw");
	tc.sendCmd("USER nopw 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(reply.empty() || tc.hasNumeric(reply, "464"))
		<< "Expected either ERR_PASSWDMISMATCH or closed connection";
}

TEST_F(IntegrationTest, UnregisteredCommand)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	tc.sendCmd("JOIN #test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "451")) << "Expected ERR_NOTREGISTERED";
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Channels
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, JoinChannel)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "joiner", "joiner");
	tc.recvAll(); /* consume welcome */

	tc.sendCmd("JOIN #inttest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_NE(reply.find("JOIN"), std::string::npos) << "Expected JOIN echo";
	EXPECT_TRUE(tc.hasNumeric(reply, "353")) << "Expected RPL_NAMREPLY";
	EXPECT_TRUE(tc.hasNumeric(reply, "366")) << "Expected RPL_ENDOFNAMES";
	EXPECT_TRUE(tc.hasNumeric(reply, "324")) << "Expected RPL_CHANNELMODEIS on join";
	EXPECT_TRUE(tc.hasNumeric(reply, "329")) << "Expected RPL_CREATIONTIME on join";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, ChannelMessage)
{
	TestClient tc1, tc2;
	ASSERT_TRUE(tc1.connect(serverPort));
	ASSERT_TRUE(tc2.connect(serverPort));

	tc1.registerClient("testpass", "sender1", "sender1");
	tc2.registerClient("testpass", "recver1", "recver1");
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("JOIN #msgtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll();

	tc2.sendCmd("JOIN #msgtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll(); /* tc1 gets tc2's JOIN */
	tc2.recvAll(); /* tc2 gets own JOIN + names */

	tc1.sendCmd("PRIVMSG #msgtest :hello from sender1");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string r2 = tc2.recvAll();
	EXPECT_NE(r2.find("hello from sender1"), std::string::npos)
		<< "Receiver should get channel message";

	tc1.sendCmd("QUIT");
	tc2.sendCmd("QUIT");
}

TEST_F(IntegrationTest, PrivateMessage)
{
	TestClient tc1, tc2;
	ASSERT_TRUE(tc1.connect(serverPort));
	ASSERT_TRUE(tc2.connect(serverPort));

	tc1.registerClient("testpass", "pmsend", "pmsend");
	tc2.registerClient("testpass", "pmrecv", "pmrecv");
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("PRIVMSG pmrecv :private hello");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string r2 = tc2.recvAll();
	EXPECT_NE(r2.find("private hello"), std::string::npos)
		<< "Should receive private message";

	tc1.sendCmd("QUIT");
	tc2.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Operator commands
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, TopicSetAndQuery)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "topicop", "topicop");
	tc.recvAll();

	tc.sendCmd("JOIN #topictest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc.recvAll();

	tc.sendCmd("TOPIC #topictest :My Cool Topic");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r1 = tc.recvAll();
	EXPECT_NE(r1.find("My Cool Topic"), std::string::npos);

	tc.sendCmd("TOPIC #topictest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r2 = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(r2, "332")) << "Expected RPL_TOPIC";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, KickUser)
{
	TestClient op, victim;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(victim.connect(serverPort));

	op.registerClient("testpass", "kickop", "kickop");
	victim.registerClient("testpass", "kicked", "kicked");
	op.recvAll();
	victim.recvAll();

	op.sendCmd("JOIN #kicktest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	victim.sendCmd("JOIN #kicktest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();
	victim.recvAll();

	op.sendCmd("KICK #kicktest kicked :bye");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string vr = victim.recvAll();
	EXPECT_NE(vr.find("KICK"), std::string::npos) << "Victim should see KICK";

	op.sendCmd("QUIT");
	victim.sendCmd("QUIT");
}

TEST_F(IntegrationTest, InviteToChannel)
{
	TestClient op, invitee;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(invitee.connect(serverPort));

	op.registerClient("testpass", "invop", "invop");
	invitee.registerClient("testpass", "invited", "invited");
	op.recvAll();
	invitee.recvAll();

	op.sendCmd("JOIN #invtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Set invite-only */
	op.sendCmd("MODE #invtest +i");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Invite the user */
	op.sendCmd("INVITE invited #invtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string opr = op.recvAll();
	EXPECT_TRUE(op.hasNumeric(opr, "341")) << "Expected RPL_INVITING";

	std::string invr = invitee.recvAll();
	EXPECT_NE(invr.find("INVITE"), std::string::npos)
		<< "Invitee should receive INVITE";

	/* Now invitee should be able to join */
	invitee.sendCmd("JOIN #invtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string jr = invitee.recvAll();
	EXPECT_NE(jr.find("JOIN"), std::string::npos) << "Invitee should join";

	op.sendCmd("QUIT");
	invitee.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Modes
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, ChannelModeQuery)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "modeq", "modeq");
	tc.recvAll();

	tc.sendCmd("JOIN #modeqtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc.recvAll();

	tc.sendCmd("MODE #modeqtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "324")) << "Expected RPL_CHANNELMODEIS";
	EXPECT_TRUE(tc.hasNumeric(reply, "329")) << "Expected RPL_CREATIONTIME";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, ChannelModeKey)
{
	TestClient op, joiner;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(joiner.connect(serverPort));

	op.registerClient("testpass", "keyop", "keyop");
	joiner.registerClient("testpass", "keyjoin", "keyjoin");
	op.recvAll();
	joiner.recvAll();

	op.sendCmd("JOIN #keytest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	op.sendCmd("MODE #keytest +k mysecret");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Try to join without key — should fail */
	joiner.sendCmd("JOIN #keytest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r1 = joiner.recvAll();
	EXPECT_TRUE(joiner.hasNumeric(r1, "475")) << "Expected ERR_BADCHANNELKEY";

	/* Join with correct key — should succeed */
	joiner.sendCmd("JOIN #keytest mysecret");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r2 = joiner.recvAll();
	EXPECT_NE(r2.find("JOIN"), std::string::npos) << "Should join with key";

	op.sendCmd("QUIT");
	joiner.sendCmd("QUIT");
}

TEST_F(IntegrationTest, ChannelModeLimit)
{
	TestClient op, joiner;
	ASSERT_TRUE(op.connect(serverPort));
	ASSERT_TRUE(joiner.connect(serverPort));

	op.registerClient("testpass", "limop", "limop");
	joiner.registerClient("testpass", "limjoin", "limjoin");
	op.recvAll();
	joiner.recvAll();

	op.sendCmd("JOIN #limtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	op.sendCmd("MODE #limtest +l 1");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	op.recvAll();

	/* Channel full — should fail */
	joiner.sendCmd("JOIN #limtest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r = joiner.recvAll();
	EXPECT_TRUE(joiner.hasNumeric(r, "471")) << "Expected ERR_CHANNELISFULL";

	op.sendCmd("QUIT");
	joiner.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Partial data reassembly
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, PartialDataReassembly)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));

	/* Send registration in fragments */
	tc.sendRaw("PASS test");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("pass\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("NI");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("CK partial1\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("USER par");
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	tc.sendRaw("tial1 0 * :Test\r\n");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "001"))
		<< "Should register after reassembled partial messages";

	tc.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Nick change
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, NickChange)
{
	TestClient tc1, tc2;
	ASSERT_TRUE(tc1.connect(serverPort));
	ASSERT_TRUE(tc2.connect(serverPort));

	tc1.registerClient("testpass", "oldnick", "oldnick");
	tc2.registerClient("testpass", "observer", "observer");
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("JOIN #nickchange");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll();

	tc2.sendCmd("JOIN #nickchange");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc1.recvAll();
	tc2.recvAll();

	tc1.sendCmd("NICK newnick");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string r2 = tc2.recvAll();
	EXPECT_NE(r2.find("NICK"), std::string::npos) << "Observer should see NICK change";
	EXPECT_NE(r2.find("newnick"), std::string::npos);

	tc1.sendCmd("QUIT");
	tc2.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Query commands
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, WhoChannel)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "whotest", "whotest");
	tc.recvAll();

	tc.sendCmd("JOIN #whotest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	tc.recvAll();

	tc.sendCmd("WHO #whotest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "352")) << "Expected RPL_WHOREPLY";
	EXPECT_TRUE(tc.hasNumeric(reply, "315")) << "Expected RPL_ENDOFWHO";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, WhoisUser)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "whoistest", "whoistest");
	tc.recvAll();

	tc.sendCmd("WHOIS whoistest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "311")) << "Expected RPL_WHOISUSER";
	EXPECT_TRUE(tc.hasNumeric(reply, "318")) << "Expected RPL_ENDOFWHOIS";

	tc.sendCmd("QUIT");
}

TEST_F(IntegrationTest, UnknownCommand)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "unkncmd", "unkncmd");
	tc.recvAll();

	tc.sendCmd("FOOBAR");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string reply = tc.recvAll();
	EXPECT_TRUE(tc.hasNumeric(reply, "421")) << "Expected ERR_UNKNOWNCOMMAND";

	tc.sendCmd("QUIT");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ServerIntegration — Bot via PRIVMSG
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(IntegrationTest, BotViaPrivmsg)
{
	TestClient tc;
	ASSERT_TRUE(tc.connect(serverPort));
	tc.registerClient("testpass", "botuser", "botuser");
	tc.recvAll();

	tc.sendCmd("PRIVMSG ircbot :!help");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	std::string reply = tc.recvAll();
	EXPECT_NE(reply.find("Available commands"), std::string::npos)
		<< "Should receive bot help response";

	tc.sendCmd("QUIT");
}
