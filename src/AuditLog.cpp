#include "AuditLog.hpp"

#include <ctime>

AuditLog::AuditLog(const std::string &path)
	: _out()
{
	/* Detect a fresh/empty file so we only write the header once. */
	bool needHeader = true;
	{
		std::ifstream probe(path.c_str());
		if (probe.good() && probe.peek() != std::ifstream::traits_type::eof())
			needHeader = false;
	}

	_out.open(path.c_str(), std::ios::out | std::ios::app);
	if (_out.is_open() && needHeader)
		_out << "timestamp,event,actor,detail\n" << std::flush;
}

AuditLog::~AuditLog()
{
	if (_out.is_open())
		_out.close();
}

/* ─── IServerExtension ─── */

const char *AuditLog::name() const
{
	return "audit-log";
}

void AuditLog::onAudit(const std::string &event, const std::string &actor,
					   const std::string &detail)
{
	log(event, actor, detail);
}

bool AuditLog::ok() const
{
	return _out.is_open();
}

std::string AuditLog::timestamp()
{
	std::time_t now = std::time(NULL);
	std::tm *lt = std::localtime(&now);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", lt);
	return std::string(buf);
}

std::string AuditLog::escape(const std::string &field)
{
	bool needQuote = field.find(',') != std::string::npos
				  || field.find('"') != std::string::npos
				  || field.find('\n') != std::string::npos;
	if (!needQuote)
		return field;

	std::string out = "\"";
	for (std::string::size_type i = 0; i < field.size(); ++i)
	{
		if (field[i] == '"')
			out += "\"\""; /* CSV doubles embedded quotes */
		else
			out += field[i];
	}
	out += "\"";
	return out;
}

void AuditLog::log(const std::string &event, const std::string &actor,
				   const std::string &detail)
{
	if (!_out.is_open())
		return;
	_out << timestamp() << ','
		 << escape(event) << ','
		 << escape(actor) << ','
		 << escape(detail) << '\n'
		 << std::flush;
}
