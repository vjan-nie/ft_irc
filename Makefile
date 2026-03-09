NAME		= ircserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98
INCLUDES	= -I include

SRCDIR		= src
OBJDIR		= obj

SRCS		= $(SRCDIR)/main.cpp \
			  $(SRCDIR)/Server.cpp \
			  $(SRCDIR)/Client.cpp \
			  $(SRCDIR)/Channel.cpp \
			  $(SRCDIR)/Message.cpp \
			  $(SRCDIR)/CommandRegistration.cpp \
			  $(SRCDIR)/CommandChannel.cpp \
			  $(SRCDIR)/CommandMessaging.cpp \
			  $(SRCDIR)/CommandOperator.cpp \
			  $(SRCDIR)/CommandQuery.cpp \
			  $(SRCDIR)/Bot.cpp

OBJS		= $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
