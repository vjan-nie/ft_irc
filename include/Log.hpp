#ifndef LOG_HPP
# define LOG_HPP

# include <string>

/*
** Log — server-side console logging.
**
** The kernel ships a plain-iostream renderer (zero non-core dependencies, so
** the mandatory tier links nothing extra). A fancier renderer can be plugged
** in at startup via Log::setSink — the full tier installs FancyLogSink,
** which renders through libcpp's TermWriter (markdown-style coloured
** callouts + banner).
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
	/* One log line: kind is 'b'anner, 'i'nfo, 's'uccess, 'w'arn, 'e'rror. */
	class ILogSink
	{
	public:
		virtual ~ILogSink() {}
		virtual void write(char kind, const std::string &msg) = 0;
	};

	/* Install a renderer (Log takes ownership; pass NULL to restore the
	** plain fallback). */
	void	setSink(ILogSink *sink);

	void	banner(const std::string &title);
	void	info(const std::string &msg);
	void	success(const std::string &msg);
	void	warn(const std::string &msg);
	void	error(const std::string &msg);
}

#endif /* LOG_HPP */
