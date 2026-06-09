# ────────────────────────────────────────────────────────────────
#  tests/Makefile – builds & runs the test_runner binary
#  Compiles with C++17 (required by Google Test 1.16.0)
# ────────────────────────────────────────────────────────────────

NAME		= test_runner

CXX			= c++
CXXFLAGS	= -Wall -Wextra -std=c++17 -g
LDFLAGS		= -lpthread

# ── Include paths ──────────────────────────────────────────────
INC_PROJ	= -I ../include
INC_GTEST	= -I ../vendor/googletest/googletest/include \
			  -I ../vendor/googletest/googletest
INC_PM		= -I ../vendor
INC_LIBCPP	= -I ../vendor/libcpp/include -I ../vendor/libcpp/c98/include
INCLUDES	= $(INC_PROJ) $(INC_GTEST) $(INC_PM) $(INC_LIBCPP)

# ── libcpp str/term modules used by the project sources ───────
LIBCPP		= ../vendor/libcpp
LIBCPP_SRCS	= $(LIBCPP)/src/str/format.cpp \
			  $(LIBCPP)/src/str/case.cpp \
			  $(LIBCPP)/src/str/utf8.cpp \
			  $(LIBCPP)/src/str/secure.cpp \
			  $(LIBCPP)/src/util/config.cpp \
			  $(LIBCPP)/src/term/color.cpp \
			  $(LIBCPP)/src/term/style.cpp \
			  $(LIBCPP)/src/term/table.cpp \
			  $(LIBCPP)/src/term/stylesheet.cpp \
			  $(LIBCPP)/src/term/writer.cpp

# ── libcpp C++98 tier ──────────────────────────────────────────
LIBCPP98_SRCS	= $(LIBCPP)/c98/src/line_buffer.cpp \
				  $(LIBCPP)/c98/src/csv_writer.cpp \
				  $(LIBCPP)/c98/src/reactor.cpp \
				  $(LIBCPP)/c98/src/buffered_socket.cpp

# ── Project sources (everything except main.cpp) ──────────────
PROJ_SRCS	= ../src/tiers/tier_full.cpp \
			  ../src/Server.cpp \
			  ../src/Log.cpp \
			  ../src/extras/FancyLogSink.cpp \
			  ../src/PlatformBus.cpp \
			  ../src/AuditLog.cpp \
			  ../src/Client.cpp \
			  ../src/Channel.cpp \
			  ../src/Message.cpp \
			  ../src/IrcCase.cpp \
			  ../src/CommandRegistration.cpp \
			  ../src/CommandChannel.cpp \
			  ../src/CommandMessaging.cpp \
			  ../src/CommandOperator.cpp \
			  ../src/CommandQuery.cpp \
			  ../src/Bot.cpp

# ── Test sources ──────────────────────────────────────────────
TEST_SRCS	= test_main.cpp \
			  test_message.cpp \
			  test_client.cpp \
			  test_channel.cpp \
			  test_bot.cpp \
			  test_integration.cpp \
			  test_robustness.cpp \
			  test_security.cpp

# ── Vendor sources ────────────────────────────────────────────
GTEST_SRC	= ../vendor/googletest/googletest/src/gtest-all.cc
PM_SRC		= ../vendor/PostMan.cpp

# ── Object directory ──────────────────────────────────────────
OBJDIR		= obj

PROJ_OBJS		= $(patsubst ../src/%.cpp,$(OBJDIR)/proj_%.o,$(PROJ_SRCS))
LIBCPP_OBJS		= $(patsubst $(LIBCPP)/src/%.cpp,$(OBJDIR)/libcpp/%.o,$(LIBCPP_SRCS))
LIBCPP98_OBJS	= $(patsubst $(LIBCPP)/c98/src/%.cpp,$(OBJDIR)/libcpp98/%.o,$(LIBCPP98_SRCS))
TEST_OBJS	= $(patsubst %.cpp,$(OBJDIR)/%.o,$(TEST_SRCS))
GTEST_OBJ	= $(OBJDIR)/gtest-all.o
PM_OBJ		= $(OBJDIR)/PostMan.o

ALL_OBJS	= $(PROJ_OBJS) $(LIBCPP_OBJS) $(LIBCPP98_OBJS) $(TEST_OBJS) $(GTEST_OBJ) $(PM_OBJ)

# ══════════════════════════════════════════════════════════════
#  Targets
# ══════════════════════════════════════════════════════════════

all: $(NAME)
	@echo "\n══════ Running tests ══════\n"
	./$(NAME)

$(NAME): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) $(ALL_OBJS) -o $(NAME) $(LDFLAGS)

# Project sources
$(OBJDIR)/proj_%.o: ../src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# libcpp str/term modules
$(OBJDIR)/libcpp/%.o: $(LIBCPP)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# libcpp C++98 tier
$(OBJDIR)/libcpp98/%.o: $(LIBCPP)/c98/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Test sources
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(ALL_OBJS:.o=.d)

# Google Test
$(GTEST_OBJ): $(GTEST_SRC)
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INC_GTEST) -c $< -o $@

# PostMan
$(PM_OBJ): $(PM_SRC)
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INC_PM) -c $< -o $@

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
