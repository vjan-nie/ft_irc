#include "extras/FancyLogSink.hpp"

#include "libcpp/term/writer.hpp"
#include "libcpp/term/style.hpp"

#include <iostream>

/*
** Each call spins up a fresh TermWriter bound to the target stream, emits a
** single styled element, and flushes. Logging happens on connection-level
** events (not per IRC message), so the cost is irrelevant and we avoid any
** shared mutable state.
*/

FancyLogSink::FancyLogSink() {}

FancyLogSink::~FancyLogSink() {}

void FancyLogSink::write(char kind, const std::string &msg)
{
	std::ostream		&os = (kind == 'w' || kind == 'e') ? std::cerr : std::cout;
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
