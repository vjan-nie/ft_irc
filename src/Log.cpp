#include "Log.hpp"

#include <iostream>

/*
** Plain-iostream renderer — the only logging the kernel needs. The full
** build swaps in FancyLogSink (src/extras/) at startup via setSink; the
** mandatory tier never links any terminal-styling code.
*/

namespace
{
	Log::ILogSink	*g_sink = 0;

	void	fallback(char kind, const std::string &msg)
	{
		switch (kind)
		{
			case 'b':
				std::cout << "== " << msg << " ==" << std::endl;
				break;
			case 'i':
				std::cout << "[ircserv] info: " << msg << std::endl;
				break;
			case 's':
				std::cout << "[ircserv] ok:   " << msg << std::endl;
				break;
			case 'w':
				std::cerr << "[ircserv] warn: " << msg << std::endl;
				break;
			case 'e':
				std::cerr << "[ircserv] error: " << msg << std::endl;
				break;
		}
	}

	void	render(char kind, const std::string &msg)
	{
		if (g_sink)
			g_sink->write(kind, msg);
		else
			fallback(kind, msg);
	}
}

void	Log::setSink(ILogSink *sink)
{
	delete g_sink;
	g_sink = sink;
}

void	Log::banner(const std::string &title)
{
	render('b', title);
}

void	Log::info(const std::string &msg)
{
	render('i', msg);
}

void	Log::success(const std::string &msg)
{
	render('s', msg);
}

void	Log::warn(const std::string &msg)
{
	render('w', msg);
}

void	Log::error(const std::string &msg)
{
	render('e', msg);
}
