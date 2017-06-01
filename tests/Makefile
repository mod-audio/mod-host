# compiler
CC = gcc

# linker
LD = gcc

# language file extension
EXT = c

# source files directory
SRC_DIR = .

# program name
PROG = effectlib_test

# default compiler and linker flags
CFLAGS += -O3 -Wall -Wextra -c -std=gnu99 -fPIC -D_GNU_SOURCE
LDFLAGS += -Wl,--no-undefined

# debug mode compiler and linker flags
ifeq ($(DEBUG), 1)
   CFLAGS += -O0 -g -Wall -Wextra -c -DDEBUG
   LDFLAGS +=
else
   CFLAGS += -fvisibility=hidden
   LDFLAGS += -s
endif

# library links
LIBS = -ljack -ldl `pkg-config --libs lilv-0` -lreadline -lpthread

# additional include paths
INCS = -I/usr/include/lilv-0 -I../src

# control chain support
ifeq ($(shell pkg-config --exists cc_client && echo true), true)
LIBS += $(shell pkg-config --libs cc_client)
INCS += $(shell pkg-config --cflags cc_client) -DHAVE_CONTROLCHAIN
endif

# hylia/link support
ifeq ($(shell pkg-config --exists hylia && echo true), true)
LIBS += $(shell pkg-config --libs hylia)
INCS += $(shell pkg-config --cflags hylia) -DHAVE_HYLIA
endif

# remove command
RM = rm -f

# source and object files
SRC = effectlib_test.c
OBJ = $(SRC:.$(EXT)=.o) $(shell ls ../src/*.o ../src/*/*.o | grep -v mod-host.o)

# linking rule
$(PROG): $(OBJ)
	$(LD) $(LDFLAGS) $(OBJ) -o $(PROG) $(LIBS)

rtmempool-test: rtmempool-test.c ../src/rtmempool/*
	$(CC) $< $(CFLAGS) $(LDFLAGS) -lrt -lpthread -o $@

rtmempool-run: rtmempool-test
	valgrind --leak-check=full --show-reachable=yes ./$<

# meta-rule to generate the object files
%.o: %.$(EXT)
	$(CC) $(CFLAGS) -c $(INCS) -o $@ $<

# clean rule
clean:
	$(RM) $(SRC_DIR)/*.o $(PROG)
