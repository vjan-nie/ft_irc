/* ─── Security & robustness hardening tests ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "TestHarness.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "IrcCase.hpp"
#include "Replies.hpp"
#include "libcpp/str/secure.hpp"

/* Fixture: server in a background thread (see TestHarness.hpp) */
class SecurityTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17300; }
};

/* ════════════════════════════════════════════════════════════════════════
 * Suite: LineInjection — CR/LF/NUL can never smuggle extra IRC lines
 * ════════════════════════════════════════════════════════════════════ */

TEST(LineInjection, EmbeddedCRStrippedFromLine)
{
	Client c(60, "127.0.0.1");
	/* A stray \r mid-line must not survive into the parsed message — a
	 * relayed "text\rQUIT" would let peers' parsers see a forged line. */
	c.appendToRecvBuffer("PRIVMSG #chan :hello\rQUIT :bye\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], "PRIVMSG #chan :helloQUIT :bye");
	EXPECT_EQ(msgs[0].find('\r'), std::string::npos);
}

TEST(LineInjection, NulByteStripped)
{
	Client c(61, "127.0.0.1");
	std::string raw("PRIVMSG #chan :he");
	raw += '\0';
	raw += "llo\r\n";
	c.appendToRecvBuffer(raw);
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], "PRIVMSG #chan :hello");
	EXPECT_EQ(msgs[0].find('\0'), std::string::npos);
}

TEST(LineInjection, CRLFSplitsAreSeparateMessagesNotInjection)
{
	Client c(62, "127.0.0.1");
	/* Full CRLF inside the stream is simply a line boundary — both halves
	 * are dispatched as their own commands through the normal gate. */
	c.appendToRecvBuffer("PRIVMSG #chan :a\r\nQUIT :bye\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 2u);
	EXPECT_EQ(msgs[0], "PRIVMSG #chan :a");
	EXPECT_EQ(msgs[1], "QUIT :bye");
}

TEST(LineInjection, CtcpDccPayloadPassesUntouched)
{
	Client c(63, "127.0.0.1");
	/* \x01-framed CTCP (DCC SEND handshakes) must relay byte-for-byte. */
	std::string ctcp = "PRIVMSG bob :\x01" "DCC SEND file.bin 2130706433 5000 1234\x01";
	c.appendToRecvBuffer(ctcp + "\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], ctcp);
}

TEST(LineInjection, HighBitBytesPreserved)
{
	Client c(64, "127.0.0.1");
	std::string text = "PRIVMSG #chan :caf\xC3\xA9 \xFF\xFE";
	c.appendToRecvBuffer(text + "\r\n");
	std::vector<std::string> msgs = c.extractMessages();
	ASSERT_EQ(msgs.size(), 1u);
	EXPECT_EQ(msgs[0], text);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: IrcCasemap — CASEMAPPING=ascii for nicks, channels, invites
 * ════════════════════════════════════════════════════════════════════ */

TEST(IrcCasemap, ToLowerIsAsciiOnly)
{
	EXPECT_EQ(ircToLower("HeLLo-123"), "hello-123");
	/* UTF-8 bytes must NOT be folded — ascii casemapping only */
	EXPECT_EQ(ircToLower("\xC3\x89"), "\xC3\x89");
}

TEST(IrcCasemap, Equals)
{
	EXPECT_TRUE(ircEquals("Bob", "bob"));
	EXPECT_TRUE(ircEquals("#Test", "#test"));
	EXPECT_FALSE(ircEquals("bob", "bobby"));
	EXPECT_FALSE(ircEquals("bob", "bub"));
}

TEST(IrcCasemap, InviteListIsCaseInsensitive)
{
	Client op(70, "127.0.0.1");
	op.setNickname("op");
	Channel chan("#sec", &op);
	chan.addInvite("Bob");
	EXPECT_TRUE(chan.isInvited("bob"));
	EXPECT_TRUE(chan.isInvited("BOB"));
	chan.removeInvite("bOb");
	EXPECT_FALSE(chan.isInvited("Bob"));
}

TEST(IrcCasemap, FindMemberIsCaseInsensitive)
{
	Client op(71, "127.0.0.1");
	op.setNickname("Alice");
	Channel chan("#sec2", &op);
	EXPECT_EQ(chan.findMember("alice"), &op);
	EXPECT_TRUE(chan.isMember(std::string("ALICE")));
}

TEST_F(SecurityTest, NickCollisionIsCaseInsensitive)
{
	TestClient a, b;
	ASSERT_TRUE(a.connect(serverPort));
	ASSERT_TRUE(b.connect(serverPort));
	a.registerClient("testpass", "bob", "bob");
	a.recvAll();

	b.sendCmd("PASS testpass");
	b.sendCmd("NICK BOB");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string reply = b.recvAll();
	EXPECT_TRUE(b.hasNumeric(reply, "433")) << "Expected ERR_NICKNAMEINUSE, got: " << reply;
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: SendQ — per-client send-buffer cap
 * ════════════════════════════════════════════════════════════════════ */

TEST(SendQ, OverflowLatchesAndDropsExcess)
{
	Client c(80, "127.0.0.1");
	EXPECT_FALSE(c.isSendQExceeded());

	/* Queue well past MAX_SENDQ without draining */
	std::string line(500, 'x');
	for (int i = 0; i < (MAX_SENDQ / 500) + 10; ++i)
		c.queueMessage(line);

	EXPECT_TRUE(c.isSendQExceeded());
	/* The buffer never grows past the cap — excess lines were dropped */
	EXPECT_LE(c.getSendBuffer().size(), static_cast<size_t>(MAX_SENDQ));
}

TEST(SendQ, NormalTrafficNeverTrips)
{
	Client c(81, "127.0.0.1");
	for (int i = 0; i < 50; ++i)
	{
		c.queueMessage("PRIVMSG #chan :normal message");
		c.clearSendBuffer(c.getSendBuffer().size()); /* drained by epoll */
	}
	EXPECT_FALSE(c.isSendQExceeded());
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ConstTime — timing-safe password comparison
 * ════════════════════════════════════════════════════════════════════ */

TEST(ConstTime, TruthTable)
{
	using libcpp::str::eq_consttime;
	EXPECT_TRUE(eq_consttime("", ""));
	EXPECT_TRUE(eq_consttime("secret", "secret"));
	EXPECT_FALSE(eq_consttime("secret", "secres"));
	EXPECT_FALSE(eq_consttime("secret", "Secret"));
	EXPECT_FALSE(eq_consttime("secret", ""));
	EXPECT_FALSE(eq_consttime("", "secret"));
	EXPECT_FALSE(eq_consttime("secret", "secretx"));
	EXPECT_FALSE(eq_consttime("secretx", "secret"));
	std::string nul1("a\0b", 3), nul2("a\0c", 3);
	EXPECT_FALSE(eq_consttime(nul1, nul2));
	EXPECT_TRUE(eq_consttime(nul1, std::string("a\0b", 3)));
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: ModeBounds — MODE +l / +k / TOPIC parameter validation
 * ════════════════════════════════════════════════════════════════════ */

class ModeBoundsTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17400; }

	/* Register an op and join #mb; returns the connected client. */
	void setUpOp(TestClient &op)
	{
		ASSERT_TRUE(op.connect(serverPort));
		op.registerClient("testpass", "modeop", "modeop");
		op.sendCmd("JOIN #mb");
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		op.recvAll();
	}

	std::string queryMode(TestClient &op)
	{
		op.sendCmd("MODE #mb");
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		return op.recvAll();
	}
};

TEST_F(ModeBoundsTest, LimitRejectsGarbageAndNegative)
{
	TestClient op;
	setUpOp(op);

	op.sendCmd("MODE #mb +l -5");
	op.sendCmd("MODE #mb +l 12abc");
	op.sendCmd("MODE #mb +l 99999999999999999999");
	op.sendCmd("MODE #mb +l 0");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	op.recvAll();

	std::string modes = queryMode(op);
	EXPECT_TRUE(op.hasNumeric(modes, "324")) << modes;
	EXPECT_EQ(modes.find('l'), std::string::npos)
		<< "no limit should be set: " << modes;
}

TEST_F(ModeBoundsTest, LimitAcceptsCanonicalNumber)
{
	TestClient op;
	setUpOp(op);

	op.sendCmd("MODE #mb +l 050");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string echo = op.recvAll();
	/* The broadcast echoes the canonical parsed value, not the raw text */
	EXPECT_NE(echo.find("+l 50"), std::string::npos) << echo;
}

TEST_F(ModeBoundsTest, KeyRejectsSpacesAndControls)
{
	TestClient op;
	setUpOp(op);

	op.sendCmd("MODE #mb +k bad,key");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string reply = op.recvAll();
	EXPECT_TRUE(op.hasNumeric(reply, "525")) << "Expected ERR_INVALIDKEY: " << reply;

	std::string modes = queryMode(op);
	EXPECT_EQ(modes.find("+k"), std::string::npos) << modes;
}

TEST_F(ModeBoundsTest, KeyRejectsOverlong)
{
	TestClient op;
	setUpOp(op);

	op.sendCmd("MODE #mb +k " + std::string(40, 'x'));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string reply = op.recvAll();
	EXPECT_TRUE(op.hasNumeric(reply, "525")) << reply;
}

TEST_F(ModeBoundsTest, TopicTruncatedToAdvertisedLimit)
{
	TestClient op;
	setUpOp(op);

	std::string longTopic(600, 'T');
	op.sendCmd("TOPIC #mb :" + longTopic);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	op.recvAll();

	op.sendCmd("TOPIC #mb");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string reply = op.recvAll();

	/* 332 reply carries the stored topic: exactly 390 'T's, not 600 */
	EXPECT_NE(reply.find(std::string(390, 'T')), std::string::npos) << reply;
	EXPECT_EQ(reply.find(std::string(391, 'T')), std::string::npos) << reply;
}

TEST_F(SecurityTest, ChannelNamesAreCaseInsensitive)
{
	TestClient a, b;
	ASSERT_TRUE(a.connect(serverPort));
	ASSERT_TRUE(b.connect(serverPort));
	a.registerClient("testpass", "casea", "casea");
	b.registerClient("testpass", "caseb", "caseb");
	a.recvAll();
	b.recvAll();

	a.sendCmd("JOIN #Mixed");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	a.recvAll();

	/* Joining with different case lands in the SAME channel... */
	b.sendCmd("JOIN #mixed");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	std::string bJoin = b.recvAll();
	EXPECT_NE(bJoin.find("casea"), std::string::npos)
		<< "NAMES should list the existing member: " << bJoin;

	/* ...and messages cross the case boundary. */
	b.sendCmd("PRIVMSG #MIXED :hello across cases");
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	std::string aMsg = a.recvAll();
	EXPECT_NE(aMsg.find("hello across cases"), std::string::npos) << aMsg;
}
