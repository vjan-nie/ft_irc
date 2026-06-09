#ifndef REGISTEREXTENSIONS_HPP
# define REGISTEREXTENSIONS_HPP

class Server;

/*
** Defined once per build tier (src/tiers/tier_<name>.cpp):
**   mandatory — registers nothing (pure RFC kernel)
**   bonus     — bot + file transfer
**   full      — bonus set + config-gated platform bus / audit / console
**
** main() calls this between constructing the Server and run().
*/
void	registerExtensions(Server &server);

#endif /* REGISTEREXTENSIONS_HPP */
