#ifndef CHANNEL_HPP
# define CHANNEL_HPP

class   Channel
{
    private:
        std::map    *users_per_channel;     // map 2D that takes user references regsitered by channel
    public:
        forward_fetch(std::vector& users_channel);
        /**
        Eject a client from the channel
        */
        kick();
        /**
        invite a client to a channel
        */
        invite();
        /**
        Change or view the channel topic
        */
        topic();
        /**
        Change the channel's  mode :
        -i : Set / Remove: Invite-only Channel
        - t: Set / Remove: The restrictions of the TOPIC command to channel operators
        - k: Set / remove the channel key (password)
        - o: Give/take channel operator privilege
        - l: set/remve the user to limit channel 
        */
        mode();
};
#endif