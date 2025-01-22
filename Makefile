# build dynamic library with -fPIC -shared
CXXFLAGS = -g # -O3 -fPIC # CXXFLAGS for .cpp
CPPFLAGS = -MMD -MP # -I../foo -DNDEBUG
LDFLAGS  = # -L../foo -shared -static
LDLIBS   = # -lfoo
CC       = $(CXX) # link with CXX for .cpp

all: server client
server: $(patsubst %.cpp,%.o,server.cpp) # .cpp
client: $(patsubst %.cpp,%.o,client.cpp) # .cpp

# target name is basename of one of the source files
# main: $(patsubst %.c,%.o,$(wildcard *.c)) # .cpp
-include *.d
clean: ; $(RM) *.o *.d server client
.PHONY: all clean
