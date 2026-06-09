/* ─── Tier: bonus ───
**
** Mandatory kernel + the subject's bonus features: the bot and file
** transfer. Built by `make bonus`.
*/

#include "ext/RegisterExtensions.hpp"
#include "Server.hpp"
#include "Bot.hpp"
#include "Log.hpp"

#include <new>

void registerExtensions(Server &server)
{
	try
	{
		server.addExtension(new Bot(&server));
	}
	catch (const std::bad_alloc &)
	{
		Log::warn("could not create bot (out of memory)");
	}
}
