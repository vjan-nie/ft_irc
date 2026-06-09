/* ─── Tier: full (default `make`) ───
**
** Bonus set + the optional real-time platform features, still runtime-gated:
** they activate only when the env var FT_IRC_CONFIG points to an INI file.
** Without it, this binary behaves byte-for-byte like the bonus tier.
**
**   [bus]
**   enabled = true
**   port    = 6700
**   secret  = change-me
**   nick    = platform
**
**   [audit]
**   enabled = true
**   path    = ./ircserv-audit.csv
*/

#include "ext/RegisterExtensions.hpp"
#include "Server.hpp"
#include "Bot.hpp"
#include "PlatformBus.hpp"
#include "AuditLog.hpp"
#include "Log.hpp"
#include "extras/FancyLogSink.hpp"
#include "libcpp/util/config.hpp"

#include <cstdlib>
#include <new>

static void registerPlatformFeatures(Server &server)
{
	const char *cfgPath = std::getenv("FT_IRC_CONFIG");
	if (!cfgPath)
		return;

	libcpp::util::Config cfg;
	if (!cfg.load_file(cfgPath))
	{
		Log::warn(std::string("could not read FT_IRC_CONFIG: ") + cfgPath);
		return;
	}

	if (cfg.get_bool("audit", "enabled", false))
	{
		std::string path = cfg.get("audit", "path", "./ircserv-audit.csv");
		AuditLog *audit = NULL;
		try
		{
			audit = new AuditLog(path);
		}
		catch (const std::bad_alloc &)
		{
			audit = NULL;
		}
		if (audit && !audit->ok())
		{
			Log::warn("could not open audit log: " + path);
			delete audit;
		}
		else if (audit)
		{
			server.addExtension(audit);
			Log::info("audit log: " + path);
		}
	}

	if (cfg.get_bool("bus", "enabled", false))
	{
		int port = cfg.get_int("bus", "port", 6700);
		std::string secret = cfg.get("bus", "secret", "");
		std::string nick = cfg.get("bus", "nick", "platform");

		try
		{
			/* listens lazily via onServerStart, once run() begins */
			server.addExtension(new PlatformBus(&server, port, secret, nick));
		}
		catch (const std::bad_alloc &)
		{
			Log::warn("could not create platform bus (out of memory)");
		}
	}
}

void registerExtensions(Server &server)
{
	try
	{
		Log::setSink(new FancyLogSink());
	}
	catch (const std::bad_alloc &)
	{
		/* plain fallback stays active */
	}

	try
	{
		server.addExtension(new Bot(&server));
	}
	catch (const std::bad_alloc &)
	{
		Log::warn("could not create bot (out of memory)");
	}

	registerPlatformFeatures(server);
}
