#ifndef AUDITLOG_HPP
# define AUDITLOG_HPP

# include <string>
# include <fstream>

# include "ext/IServerExtension.hpp"

/*
** AuditLog — append-only CSV trail of server activity (connections, joins,
** disconnects, published events). Useful for the platform's compliance/history
** view. Config-gated: only created when [audit] enabled = true, so the plain
** RFC server writes nothing to disk.
**
** Plugged in through the extension seam: Server::audit() fans out to
** onAudit, which appends one CSV row.
**
** Columns: timestamp,event,actor,detail  (RFC-3339-ish local time)
*/
class AuditLog : public IServerExtension
{
public:
	explicit AuditLog(const std::string &path);
	~AuditLog();

	/* ─── IServerExtension ─── */
	const char	*name() const;
	void		onAudit(const std::string &event, const std::string &actor,
						const std::string &detail);

	bool	ok() const;
	void	log(const std::string &event, const std::string &actor,
				const std::string &detail);

private:
	AuditLog();
	AuditLog(const AuditLog &other);
	AuditLog &operator=(const AuditLog &other);

	static std::string	timestamp();
	static std::string	escape(const std::string &field);

	std::ofstream	_out;
};

#endif /* AUDITLOG_HPP */
