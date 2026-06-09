#ifndef FANCYLOGSINK_HPP
# define FANCYLOGSINK_HPP

# include "Log.hpp"

/*
** FancyLogSink — console renderer for the full tier, drawing through
** libcpp's TermWriter (markdown-style coloured callouts + h1 banner).
** Installed at startup by the full tier's registerExtensions() via
** Log::setSink(new FancyLogSink). The mandatory/bonus tiers never link
** this file (nor libcpp/term).
*/
class FancyLogSink : public Log::ILogSink
{
public:
	FancyLogSink();
	~FancyLogSink();

	void	write(char kind, const std::string &msg);
};

#endif /* FANCYLOGSINK_HPP */
