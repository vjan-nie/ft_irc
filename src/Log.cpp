#include "Log.hpp"
#include "libcpp/term/writer.hpp"
#include "libcpp/term/style.hpp"

#include <iostream>

/*
** Each call spins up a fresh TermWriter bound to the target stream, emits a
** single styled element, and flushes. Logging happens on connection-level
** events (not per IRC message), so the cost is irrelevant and we avoid any
** shared mutable state.
*/

namespace
{
	/* helper: render one element of the given kind, then flush */
	void	render(std::ostream &os, char kind, const std::string &msg)
	{
		libcpp::TermStyle	ts;
		libcpp::TermWriter	w(ts, os);

		switch (kind)
		{
			case 'b': w.h1(msg); break;
			case 'i': w.info(msg); break;
			case 's': w.success(msg); break;
			case 'w': w.warn(msg); break;
			case 'e': w.error(msg); break;
		}
		w.flush();
	}
}

void	Log::banner(const std::string &title)
{
	render(std::cout, 'b', title);
}

void	Log::info(const std::string &msg)
{
	render(std::cout, 'i', msg);
}

void	Log::success(const std::string &msg)
{
	render(std::cout, 's', msg);
}

void	Log::warn(const std::string &msg)
{
	render(std::cerr, 'w', msg);
}

void	Log::error(const std::string &msg)
{
	render(std::cerr, 'e', msg);
}
