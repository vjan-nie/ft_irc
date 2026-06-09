/* ─── Security & robustness hardening tests ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "TestHarness.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "IrcCase.hpp"
#include "Replies.hpp"

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
