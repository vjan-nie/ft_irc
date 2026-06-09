#include "IrcCase.hpp"

std::string ircToLower(const std::string &s)
{
	std::string out(s);
	for (std::string::size_type i = 0; i < out.size(); ++i)
	{
		if (out[i] >= 'A' && out[i] <= 'Z')
			out[i] = static_cast<char>(out[i] - 'A' + 'a');
	}
	return out;
}

bool ircEquals(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	return ircToLower(a) == ircToLower(b);
}
