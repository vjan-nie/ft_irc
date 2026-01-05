#ifndef CLIENT_HPP
# define CLIENT_HPP

# include "common.hpp"

class Client 
{
    private:
        std::string&    nickname;
        std::string&    userName;
        std::vector     channels;
        const std::name&      refClient;

    public:
        void    send();
        void    get();
}
# endif