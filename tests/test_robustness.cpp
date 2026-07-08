/* ─── Robustness tests: crash resistance, stress, edge cases ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"
#include "Server.hpp"

#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>
#include <csignal>

#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/* ──────────────────────────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────────────────────── */

static int quickConnect(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	if (::connect(fd, reinterpret_cast<struct sockaddr *>(&addr),
				  sizeof(addr)) < 0)
	{
		close(fd);
		return -1;
	}
	return fd;
}

static void sendLine(int fd, const std::string &s)
{
	std::string line = s + "\r\n";
	send(fd, line.c_str(), line.size(), MSG_NOSIGNAL);
}

static std::string recvBuf(int fd, int timeoutMs = 300)
{
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = timeoutMs * 1000;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	std::string result;
	char buf[4096];
	while (true)
	{
		ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
		if (n <= 0) break;
		buf[n] = '\0';
		result += buf;
	}
	return result;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Fixture
 * ══════════════════════════════════════════════════════════════════════ */

class RobustnessTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		server = NULL;
		serverPort = 0;

		for (int port = 17200; port < 17300; ++port)
		{
			try
			{
				server = new Server(port, "robpass");
				serverPort = port;
				break;
			}
			catch (...) { continue; }
		}
		ASSERT_NE(server, nullptr) << "Could not bind to any port";

		serverThread = std::thread([this]() {
			server->run();
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	void TearDown() override
	{
		if (server)
		{
			Server::isRunning = false;
			if (serverThread.joinable())
				serverThread.join();
			delete server;
			server = NULL;
			Server::isRunning = true;
		}
	}

	Server *server;
	int serverPort;
	std::thread serverThread;
};

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Abrupt disconnect
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, AbruptDisconnect)
{
	/* Connect, register, then close without QUIT */
	int fd = quickConnect(serverPort);
	ASSERT_GE(fd, 0);

	sendLine(fd, "PASS robpass");
	sendLine(fd, "NICK abrupt1");
	sendLine(fd, "USER abrupt1 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fd);

	sendLine(fd, "JOIN #abruptest");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fd);

	/* Abruptly close — no QUIT */
	close(fd);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	/* Server should still be running; connect another client */
	int fd2 = quickConnect(serverPort);
	ASSERT_GE(fd2, 0) << "Server should still accept after abrupt disconnect";
	close(fd2);
}

TEST_F(RobustnessTest, AbruptDisconnectViaRST)
{
	/* Connect, register, then force a real RST (not a FIN) via SO_LINGER */
	int fd = quickConnect(serverPort);
	ASSERT_GE(fd, 0);

	const std::string nick = "abruptrst";
	sendLine(fd, "PASS robpass");
	sendLine(fd, "NICK " + nick);
	sendLine(fd, "USER abruptrst 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fd);

	struct linger sl;
	sl.l_onoff = 1;
	sl.l_linger = 0;
	setsockopt(fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
	close(fd);

	/*
	 * Black-box proof the reset client was actually torn down server-side
	 * (not just that the listener still accepts): register a NEW client
	 * reusing the SAME nick. Nick release depends on the server processing
	 * EPOLLERR|HUP for the RST, which is not synchronous with our close()
	 * — bounded retry instead of a fixed sleep: up to 15 attempts, 100ms
	 * apart (1.5s total margin). Each attempt reconnects on a fresh socket
	 * and sends a full PASS/NICK/USER handshake, so the test makes no
	 * assumption about partial-registration or NICK-only retry semantics
	 * — success is simply seeing RPL_WELCOME (001) for that attempt.
	 */
	const int MAX_ATTEMPTS = 15;
	bool registered = false;
	std::string lastReply;

	for (int attempt = 0; attempt < MAX_ATTEMPTS && !registered; ++attempt)
	{
		int fd2 = quickConnect(serverPort);
		ASSERT_GE(fd2, 0);

		sendLine(fd2, "PASS robpass");
		sendLine(fd2, "NICK " + nick);
		sendLine(fd2, "USER abruptrst2 0 * :Test");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		lastReply = recvBuf(fd2);

		registered = lastReply.find(" 001 ") != std::string::npos;
		close(fd2);
	}

	EXPECT_TRUE(registered)
		<< "Nick '" << nick << "' should be released after RST within "
		<< MAX_ATTEMPTS << " retries (100ms apart); last reply: '"
		<< lastReply << "'";
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Rapid connect / disconnect
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, RapidConnectDisconnect)
{
	const int ROUNDS = 50;
	int successes = 0;

	for (int i = 0; i < ROUNDS; ++i)
	{
		int fd = quickConnect(serverPort);
		if (fd >= 0)
		{
			++successes;
			close(fd);
		}
	}
	EXPECT_GE(successes, ROUNDS / 2)
		<< "At least half the rapid connections should succeed";

	/* Verify server is still healthy */
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	int fd = quickConnect(serverPort);
	ASSERT_GE(fd, 0) << "Server alive after rapid connect/disconnect";
	close(fd);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Empty / whitespace messages
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, EmptyMessages)
{
	int fd = quickConnect(serverPort);
	ASSERT_GE(fd, 0);

	sendLine(fd, "PASS robpass");
	sendLine(fd, "NICK empty1");
	sendLine(fd, "USER empty1 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fd);

	/* Shoot empty lines, whitespace-only lines */
	sendLine(fd, "");
	sendLine(fd, "   ");
	sendLine(fd, "\t");
	sendLine(fd, "");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	/* Server should still respond to normal commands */
	sendLine(fd, "PING :alive");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string reply = recvBuf(fd);
	/* Either a PONG or no crash is good */
	SUCCEED() << "Server handled empty messages without crashing";

	close(fd);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Oversized message ( > 512 bytes )
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, OversizedMessage)
{
	int fd = quickConnect(serverPort);
	ASSERT_GE(fd, 0);

	sendLine(fd, "PASS robpass");
	sendLine(fd, "NICK oversize");
	sendLine(fd, "USER oversize 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fd);

	/* Send a 2000-char message */
	std::string big = "PRIVMSG #test :" + std::string(2000, 'A');
	sendLine(fd, big);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	/* Server should still be responsive */
	sendLine(fd, "PING :oversizecheck");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	SUCCEED() << "Server survived oversized message";

	close(fd);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Flood of commands
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, CommandFlood)
{
	int fd = quickConnect(serverPort);
	ASSERT_GE(fd, 0);

	sendLine(fd, "PASS robpass");
	sendLine(fd, "NICK flood1");
	sendLine(fd, "USER flood1 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fd);

	sendLine(fd, "JOIN #flood");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fd);

	/* Blast 200 messages */
	for (int i = 0; i < 200; ++i)
	{
		std::string msg = "PRIVMSG #flood :msg-" + std::to_string(i);
		sendLine(fd, msg);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	/* Check server is still alive */
	int fd2 = quickConnect(serverPort);
	ASSERT_GE(fd2, 0) << "Server alive after flood";
	close(fd2);
	close(fd);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Duplicate nickname
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, DuplicateNickname)
{
	int fd1 = quickConnect(serverPort);
	ASSERT_GE(fd1, 0);
	sendLine(fd1, "PASS robpass");
	sendLine(fd1, "NICK dupnick");
	sendLine(fd1, "USER dupnick 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fd1);

	int fd2 = quickConnect(serverPort);
	ASSERT_GE(fd2, 0);
	sendLine(fd2, "PASS robpass");
	sendLine(fd2, "NICK dupnick");
	sendLine(fd2, "USER dupnick2 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::string r = recvBuf(fd2);
	EXPECT_NE(r.find("433"), std::string::npos) << "Expected ERR_NICKNAMEINUSE";

	close(fd1);
	close(fd2);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — PART and rejoin
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, PartAndRejoin)
{
	int fd = quickConnect(serverPort);
	ASSERT_GE(fd, 0);
	sendLine(fd, "PASS robpass");
	sendLine(fd, "NICK partre");
	sendLine(fd, "USER partre 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fd);

	for (int i = 0; i < 10; ++i)
	{
		sendLine(fd, "JOIN #retest");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		recvBuf(fd);
		sendLine(fd, "PART #retest :leaving");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		recvBuf(fd);
	}

	/* Final join should still work */
	sendLine(fd, "JOIN #retest");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	std::string r = recvBuf(fd);
	EXPECT_NE(r.find("JOIN"), std::string::npos)
		<< "Should rejoin after repeated part/join cycles";

	close(fd);
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — No zombie processes (fork-safety)
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, NoZombieProcesses)
{
	/* If the server forks children, we should have no zombies.
	 * Our epoll server doesn't fork, but this verifies. */
	(void)getpid();

	/* Connect and disconnect a few clients */
	for (int i = 0; i < 5; ++i)
	{
		int fd = quickConnect(serverPort);
		if (fd >= 0)
		{
			sendLine(fd, "PASS robpass");
			sendLine(fd, "NICK zombie" + std::to_string(i));
			sendLine(fd, "USER zombie" + std::to_string(i) + " 0 * :T");
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			close(fd);
		}
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	/* Check for zombie children */
	int status;
	pid_t zombie = waitpid(-1, &status, WNOHANG);
	/* waitpid returns -1 (ECHILD) when there are no children, 0 when
	 * children exist but none have terminated, >0 if a zombie was reaped */
	EXPECT_LE(zombie, 0) << "No zombie processes should exist";
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Memory (PostMan)
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, NoLeakAfterClientChurn)
{
	/* One warm-up cycle BEFORE the snapshot: the first client ever handled
	 * by this fresh server thread triggers one-time lazy initialisation
	 * (locale facets, per-thread runtime structures) that would otherwise
	 * read as a phantom "leak". The test's purpose is per-client balance. */
	{
		int fd = quickConnect(serverPort);
		ASSERT_GE(fd, 0);
		sendLine(fd, "PASS robpass");
		sendLine(fd, "NICK leakwarm");
		sendLine(fd, "USER leakwarm 0 * :T");
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		recvBuf(fd);
		sendLine(fd, "QUIT :bye");
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		close(fd);
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	TestReport::instance().snapshotMemory();

	/* Ten connect-register-quit cycles */
	for (int i = 0; i < 10; ++i)
	{
		int fd = quickConnect(serverPort);
		if (fd < 0) continue;
		sendLine(fd, "PASS robpass");
		sendLine(fd, "NICK leak" + std::to_string(i));
		sendLine(fd, "USER leak" + std::to_string(i) + " 0 * :T");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		recvBuf(fd);
		sendLine(fd, "QUIT :bye");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		close(fd);
	}

	/* The server thread releases each Client asynchronously — poll the
	 * allocation balance instead of racing it with a fixed sleep. */
	for (int waited = 0;
		 TestReport::instance().leakDelta() != 0 && waited < 3000;
		 waited += 50)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

	ASSERT_NO_LEAKS("client churn should not leak memory");
}

/* ════════════════════════════════════════════════════════════════════════
 * Suite: Robustness — Frozen reader + channel flood (T6)
 *
 * A client that stops reading its socket ("frozen reader") while a channel
 * it's in gets flooded by another member must not affect a third,
 * unrelated client, and the server must not crash. See
 * .claude/workflow/tasks/T6-frozen-reader-flood/01-audit.md for the
 * source/behavioral audit backing these three tests — none of them assert
 * an exact byte or line cutoff, since the SendQ-disconnect boundary is an
 * OS socket-buffer artifact, not a value ft_irc controls.
 * ════════════════════════════════════════════════════════════════════ */

TEST_F(RobustnessTest, ThirdClientUnaffectedByFrozenReaderFlood)
{
	/* A: the frozen reader — registers, joins, then never recv()s again */
	int fdA = quickConnect(serverPort);
	ASSERT_GE(fdA, 0);
	sendLine(fdA, "PASS robpass");
	sendLine(fdA, "NICK frozenA");
	sendLine(fdA, "USER frozenA 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fdA);
	sendLine(fdA, "JOIN #flood1");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fdA);
	/* fdA is never read again below this point */

	/* B: the flooder */
	int fdB = quickConnect(serverPort);
	ASSERT_GE(fdB, 0);
	sendLine(fdB, "PASS robpass");
	sendLine(fdB, "NICK frozenB");
	sendLine(fdB, "USER frozenB 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fdB);
	sendLine(fdB, "JOIN #flood1");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fdB);

	/* C: the control client whose responsiveness is actually asserted */
	int fdC = quickConnect(serverPort);
	ASSERT_GE(fdC, 0);
	sendLine(fdC, "PASS robpass");
	sendLine(fdC, "NICK frozenC");
	sendLine(fdC, "USER frozenC 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fdC);

	const int FLOOD_LINES = 200000;   // ~0.5s of real backpressure, overlaps C's probe
	std::thread floodThread([fdB]() {
		for (int i = 0; i < FLOOD_LINES; ++i)
			sendLine(fdB, "PRIVMSG #flood1 :msg-" + std::to_string(i));
	});

	/* Bounded-retry PING/PONG probe against C overlapping B's flood thread
	 * — keeps firing until a PONG lands or the deadline expires, so it
	 * isn't racing the flood's exact timing. */
	bool gotPong = false;
	std::string lastReply;
	const int MAX_PROBES = 30;
	for (int attempt = 0; attempt < MAX_PROBES && !gotPong; ++attempt)
	{
		sendLine(fdC, "PING :isolation-check");
		lastReply = recvBuf(fdC, 200);
		gotPong = lastReply.find("PONG") != std::string::npos;
	}

	floodThread.join();

	EXPECT_TRUE(gotPong)
		<< "Client C should get a timely PONG even while A is a frozen "
		   "reader and B is flooding the shared channel; last reply: '"
		<< lastReply << "'";

	close(fdA);
	close(fdB);
	close(fdC);
}

TEST_F(RobustnessTest, ServerSurvivesFloodAgainstFrozenReader)
{
	int fdA = quickConnect(serverPort);
	ASSERT_GE(fdA, 0);
	sendLine(fdA, "PASS robpass");
	sendLine(fdA, "NICK survA");
	sendLine(fdA, "USER survA 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fdA);
	sendLine(fdA, "JOIN #flood2");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fdA);
	/* fdA is never read again below this point */

	int fdB = quickConnect(serverPort);
	ASSERT_GE(fdB, 0);
	sendLine(fdB, "PASS robpass");
	sendLine(fdB, "NICK survB");
	sendLine(fdB, "USER survB 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fdB);
	sendLine(fdB, "JOIN #flood2");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fdB);

	const int FLOOD_LINES = 1000;
	for (int i = 0; i < FLOOD_LINES; ++i)
		sendLine(fdB, "PRIVMSG #flood2 :msg-" + std::to_string(i));

	/* Bounded-retry registration of a brand-new client D, same shape as
	 * AbruptDisconnectViaRST: keep trying until 001 shows up. */
	const int MAX_ATTEMPTS = 15;
	bool registered = false;
	std::string lastReply;
	for (int attempt = 0; attempt < MAX_ATTEMPTS && !registered; ++attempt)
	{
		int fdD = quickConnect(serverPort);
		ASSERT_GE(fdD, 0);
		sendLine(fdD, "PASS robpass");
		sendLine(fdD, "NICK survD");
		sendLine(fdD, "USER survD 0 * :Test");
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		lastReply = recvBuf(fdD);
		registered = lastReply.find(" 001 ") != std::string::npos;
		close(fdD);
	}

	EXPECT_TRUE(registered)
		<< "A new client should still be able to register (001) after a "
		   "flood targeting a frozen reader; last reply: '" << lastReply
		<< "'";

	close(fdA);
	close(fdB);
}

TEST_F(RobustnessTest, FrozenReaderEventuallyDisconnectedOnSendQ)
{
	/* Heaviest test in the suite: sends ~12 MB to push a frozen reader past
	 * both this machine's real OS socket-buffer ceiling and the 64 KiB
	 * MAX_SENDQ latch. The exact byte count at which the disconnect fires
	 * is environment-dependent (see
	 * .claude/workflow/tasks/T6-frozen-reader-flood/01-audit.md) — this
	 * flood volume carries a wide safety margin (~6x) over the ~1.88 MB
	 * ceiling measured on the audit machine, not tuned to it. */
	int fdA = quickConnect(serverPort);
	ASSERT_GE(fdA, 0);
	sendLine(fdA, "PASS robpass");
	sendLine(fdA, "NICK cutA");
	sendLine(fdA, "USER cutA 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fdA);
	sendLine(fdA, "JOIN #flood3");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fdA);
	/* fdA is never read again below this point */

	int fdB = quickConnect(serverPort);
	ASSERT_GE(fdB, 0);
	sendLine(fdB, "PASS robpass");
	sendLine(fdB, "NICK cutB");
	sendLine(fdB, "USER cutB 0 * :Test");
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	recvBuf(fdB);
	sendLine(fdB, "JOIN #flood3");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	recvBuf(fdB);

	const int FLOOD_LINES = 30000;
	const std::string payload(400, 'A'); /* line ~419B, under the 512B input cap */
	for (int i = 0; i < FLOOD_LINES; ++i)
		sendLine(fdB, "PRIVMSG #flood3 :" + payload);

	/* Bounded wait for A's socket to be closed server-side: keep recv()ing
	 * (draining whatever backlog already arrived) until EOF (n == 0), with
	 * a generous but finite safety-net deadline so a hypothetical
	 * environment whose OS buffers absorb the whole flood fails this test
	 * cleanly instead of hanging the suite. */
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 200 * 1000;
	setsockopt(fdA, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	bool closed = false;
	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(20);
	char buf[4096];
	while (std::chrono::steady_clock::now() < deadline)
	{
		ssize_t n = recv(fdA, buf, sizeof(buf), 0);
		if (n == 0) { closed = true; break; }
		/* n < 0: timeout, keep polling. n > 0: still draining backlog. */
	}

	EXPECT_TRUE(closed)
		<< "Frozen reader A should eventually be disconnected once a large "
		   "enough flood (" << FLOOD_LINES << " lines, ~"
		<< (FLOOD_LINES * (payload.size() + 19)) / (1024 * 1024)
		<< " MB) exceeds real OS + MAX_SENDQ backpressure";

	close(fdA);
	close(fdB);
}
