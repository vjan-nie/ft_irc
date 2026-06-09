NAME		= ircserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

SRCDIR		= src
OBJDIR		= obj

# ── libcpp: reuse the project's own C++98-clean str/term modules ──────────────
#  These sources compile cleanly under -std=c++98 -Wall -Wextra -Werror; they
#  are built from source as part of ircserv (no external library is linked).
LIBCPP		= vendor/libcpp
INCLUDES	= -I include -I $(LIBCPP)/include

LIBCPP_SRCS	= $(LIBCPP)/src/str/format.cpp \
			  $(LIBCPP)/src/str/case.cpp \
			  $(LIBCPP)/src/str/utf8.cpp \
			  $(LIBCPP)/src/util/config.cpp \
			  $(LIBCPP)/src/term/color.cpp \
			  $(LIBCPP)/src/term/style.cpp \
			  $(LIBCPP)/src/term/table.cpp \
			  $(LIBCPP)/src/term/stylesheet.cpp \
			  $(LIBCPP)/src/term/writer.cpp

SRCS		= $(SRCDIR)/main.cpp \
			  $(SRCDIR)/Server.cpp \
			  $(SRCDIR)/Log.cpp \
			  $(SRCDIR)/PlatformBus.cpp \
			  $(SRCDIR)/AuditLog.cpp \
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
LIBCPP_OBJS	= $(LIBCPP_SRCS:$(LIBCPP)/src/%.cpp=$(OBJDIR)/libcpp/%.o)

all: $(NAME)

$(NAME): $(OBJS) $(LIBCPP_OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LIBCPP_OBJS) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJDIR)/libcpp/%.o: $(LIBCPP)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d) $(LIBCPP_OBJS:.o=.d)

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

test:
	@$(MAKE) -C tests

testclean:
	@$(MAKE) -C tests fclean

.PHONY: all clean fclean re test testclean
