#ifndef LOG_HPP
# define LOG_HPP

# include <string>

/*
** Log — server-side console logging rendered through libcpp's TermWriter
** (markdown-style terminal output: coloured callouts + a startup banner).
**
** IMPORTANT: this is for the OPERATOR CONSOLE ONLY. It must never be used to
** build client-facing data — IRC clients (HexChat, nc) require raw RFC 2812
** protocol lines, which are produced with plain string concatenation and sent
** via Client::queueMessage / Channel::broadcastMessage / Server::sendToClient.
**
** info / success / banner go to stdout; warn / error go to stderr.
*/
namespace Log
{
	void	banner(const std::string &title);
	void	info(const std::string &msg);
	void	success(const std::string &msg);
	void	warn(const std::string &msg);
	void	error(const std::string &msg);
}

#endif /* LOG_HPP */
