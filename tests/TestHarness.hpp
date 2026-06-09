/* ─── Shared integration-test harness: TCP TestClient + Server fixture ───
 *
 * Extracted from test_integration.cpp so every protocol-level suite
 * (integration, security, file transfer) reuses one client and one fixture.
 * Each suite subclasses IrcServerTest with its own port base to avoid
 * cross-suite bind clashes.
 */

#ifndef TEST_HARNESS_HPP
#define TEST_HARNESS_HPP

#include <gtest/gtest.h>
#include "Server.hpp"

#include <thread>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════════════════════════
 * TestClient — lightweight TCP client for protocol-level testing
 * ══════════════════════════════════════════════════════════════════════ */

class TestClient
{
public:
	TestClient() : _fd(-1) {}
	~TestClient() { disconnect(); }

	bool connect(int port)
	{
		_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (_fd < 0) return false;

		struct sockaddr_in addr;
		std::memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

		if (::connect(_fd, reinterpret_cast<struct sockaddr *>(&addr),
					  sizeof(addr)) < 0)
		{
			close(_fd);
			_fd = -1;
			return false;
		}

		/* Set a read timeout so tests don't hang */
		struct timeval tv;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		return true;
	}

	void disconnect()
	{
		if (_fd >= 0)
		{
			close(_fd);
			_fd = -1;
		}
	}

	int fd() const { return _fd; }

	void sendRaw(const std::string &data)
	{
		if (_fd >= 0)
			send(_fd, data.c_str(), data.size(), 0);
	}

	void sendCmd(const std::string &cmd)
	{
		sendRaw(cmd + "\r\n");
	}

	std::string recvAll(int timeoutMs = 500)
	{
		std::string result;
		char buf[4096];

		/* Set shorter timeout for bulk reads */
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = timeoutMs * 1000;
		setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		while (true)
		{
			ssize_t n = recv(_fd, buf, sizeof(buf) - 1, 0);
			if (n <= 0) break;
			buf[n] = '\0';
			result += buf;
		}
		return result;
	}

	bool registerClient(const std::string &pass, const std::string &nick,
						const std::string &user)
	{
		sendCmd("PASS " + pass);
		sendCmd("NICK " + nick);
		sendCmd("USER " + user + " 0 * :Test User");
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		return true;
	}

	bool hasNumeric(const std::string &data, const std::string &numeric)
	{
		return data.find(" " + numeric + " ") != std::string::npos;
	}

private:
	int _fd;
};

/* ══════════════════════════════════════════════════════════════════════════
 * IrcServerTest — fixture running a Server in a background thread.
 * Subclass and override portBase() per suite.
 * ══════════════════════════════════════════════════════════════════════ */

class IrcServerTest : public ::testing::Test
{
protected:
	virtual int portBase() const { return 17100; }

	void SetUp() override
	{
		server = NULL;
		serverPort = 0;

		for (int port = portBase(); port < portBase() + 100; ++port)
		{
			try
			{
				server = new Server(port, "testpass");
				serverPort = port;
				break;
			}
			catch (...) { continue; }
		}
		ASSERT_NE(server, nullptr) << "Could not bind to any port";

		/* Run server in a background thread */
		serverThread = std::thread([this]() {
			server->run();
		});

		/* Give server time to start accepting */
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

#endif /* TEST_HARNESS_HPP */
