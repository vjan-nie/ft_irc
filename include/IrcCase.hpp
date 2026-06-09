#ifndef IRCCASE_HPP
# define IRCCASE_HPP

# include <string>

/* IRC casemapping (matches the CASEMAPPING=ascii token advertised in 005):
** nicknames and channel names compare case-insensitively over ASCII A-Z
** only.  Deliberately not UTF-8 aware — that would be the wrong semantics
** for the IRC protocol. */
std::string	ircToLower(const std::string &s);
bool		ircEquals(const std::string &a, const std::string &b);

#endif
