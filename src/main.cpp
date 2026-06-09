#include "Server.hpp"
#include "Log.hpp"
#include "ext/RegisterExtensions.hpp"

#include <iostream>
#include <cstdlib>
#include <csignal>

static void signalHandler(int signum)
{
	(void)signum;
	Server::isRunning = false;
	std::cout << "\nShutting down..." << std::endl;
}

static bool isNumber(const std::string &str)
{
	if (str.empty())
		return false;
	for (size_t i = 0; i < str.size(); ++i)
	{
		if (!std::isdigit(static_cast<unsigned char>(str[i])))
			return false;
	}
	return true;
}

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		Log::error("usage: ./ircserv <port> <password>");
		return 1;
	}

	std::string portStr = argv[1];
	std::string password = argv[2];

	if (!isNumber(portStr))
	{
		Log::error("port must be a number");
		return 1;
	}

	int port = std::atoi(portStr.c_str());
	if (port < 1 || port > 65535)
	{
		Log::error("port must be between 1 and 65535");
		return 1;
	}

	if (password.empty())
	{
		Log::error("password cannot be empty");
		return 1;
	}

	// Ignore SIGPIPE (critical — send() to closed socket)
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	try
	{
		Server server(port, password);
		registerExtensions(server); /* which set depends on the build tier */
		server.run();
	}
	catch (const std::exception &e)
	{
		Log::error(std::string("fatal: ") + e.what());
		return 1;
	}

	return 0;
}
