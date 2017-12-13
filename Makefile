NAME=ModMidi
CXX ?= g++
RM ?= rm -f

# library dependencies
CXXFLAGS += -std=c++11 -Wall
CXXFLAGS += `pkg-config --cflags jack jansson`
LDFLAGS += `pkg-config --libs jack jansson`

SRCS=$(wildcard src/*.cpp)
OBJS=$(subst .cpp,.o,$(SRCS))

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) -o $(NAME) $(OBJS) $(LDFLAGS)

src/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS)

distclean: clean
	$(RM) $(NAME)