#ifndef ISERVEREXTENSION_HPP
# define ISERVEREXTENSION_HPP

# include <string>
# include <ctime>
# include <stdint.h>

class Server;
class Client;
class Channel;
struct Message;

/*
** IServerExtension — the seam between the RFC 2812 kernel and everything
** optional (bot, platform bus, audit trail, fancy console).
**
** Server fires these hooks at well-defined points; it owns the registered
** extensions and deletes them in reverse registration order. Which
** extensions exist is decided purely at link time by the tier's
** registerExtensions() translation unit (src/tiers/) — the kernel sources
** never name a concrete extension and carry zero #ifdef.
**
** Interception hooks return true to mean "handled, stop processing":
**   - onCommand fires only where ERR_UNKNOWNCOMMAND would be sent, so an
**     extension can add commands but never shadow a core one.
**   - onPrivmsg fires per resolved non-channel target, before the nick
**     lookup, so a virtual participant can claim the message.
*/
class IServerExtension
{
public:
	virtual ~IServerExtension() {}

	virtual const char	*name() const = 0;

	/* ─── lifecycle ─── */
	virtual void	onServerStart(Server &server) { (void)server; }
	virtual void	onTick(Server &server, time_t now)
	{ (void)server; (void)now; }

	/* ─── client lifecycle ─── */
	virtual void	onClientRegistered(Server &server, Client &client)
	{ (void)server; (void)client; }
	virtual void	onClientDisconnect(Server &server, Client &client,
									   const std::string &reason)
	{ (void)server; (void)client; (void)reason; }

	/* ─── channel events ─── */
	virtual void	onJoin(Server &server, Client &client, Channel &channel)
	{ (void)server; (void)client; (void)channel; }
	virtual void	onPart(Server &server, Client &client, Channel &channel)
	{ (void)server; (void)client; (void)channel; }

	/* ─── interception (true = handled) ─── */
	virtual bool	onCommand(Server &server, Client &client, const Message &msg)
	{ (void)server; (void)client; (void)msg; return false; }
	virtual bool	onPrivmsg(Server &server, Client &sender,
							  const std::string &target, const std::string &text)
	{ (void)server; (void)sender; (void)target; (void)text; return false; }
	virtual bool	reservesNick(const std::string &nick) const
	{ (void)nick; return false; }

	/* ─── foreign-fd plumbing (extensions with their own sockets) ─── */
	virtual bool	ownsFd(int fd) const { (void)fd; return false; }
	virtual void	onFdEvent(Server &server, int fd, uint32_t events)
	{ (void)server; (void)fd; (void)events; }

	/* ─── audit fan-out ─── */
	virtual void	onAudit(const std::string &event, const std::string &actor,
							const std::string &detail)
	{ (void)event; (void)actor; (void)detail; }
};

#endif /* ISERVEREXTENSION_HPP */
