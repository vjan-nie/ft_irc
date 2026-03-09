#include "Message.hpp"
#include <cctype>

Message Message::parse(const std::string &raw)
{
	Message msg;
	std::string line = raw;
	std::string::size_type pos = 0;

	// Skip leading whitespace
	while (pos < line.size() && line[pos] == ' ')
		++pos;

	// Parse optional prefix (starts with ':')
	if (pos < line.size() && line[pos] == ':')
	{
		++pos;
		std::string::size_type end = line.find(' ', pos);
		if (end == std::string::npos)
		{
			msg.prefix = line.substr(pos);
			return msg;
		}
		msg.prefix = line.substr(pos, end - pos);
		pos = end;
		while (pos < line.size() && line[pos] == ' ')
			++pos;
	}

	// Parse command (uppercase it)
	if (pos < line.size())
	{
		std::string::size_type end = line.find(' ', pos);
		if (end == std::string::npos)
		{
			msg.command = line.substr(pos);
		}
		else
		{
			msg.command = line.substr(pos, end - pos);
			pos = end;
		}
		// Uppercase the command
		for (std::string::size_type i = 0; i < msg.command.size(); ++i)
			msg.command[i] = std::toupper(static_cast<unsigned char>(msg.command[i]));

		if (end == std::string::npos)
			return msg;

		while (pos < line.size() && line[pos] == ' ')
			++pos;
	}

	// Parse parameters
	while (pos < line.size())
	{
		if (line[pos] == ':')
		{
			// Trailing parameter: everything after ':'
			msg.params.push_back(line.substr(pos + 1));
			break;
		}
		std::string::size_type end = line.find(' ', pos);
		if (end == std::string::npos)
		{
			msg.params.push_back(line.substr(pos));
			break;
		}
		msg.params.push_back(line.substr(pos, end - pos));
		pos = end;
		while (pos < line.size() && line[pos] == ' ')
			++pos;
	}

	return msg;
}
