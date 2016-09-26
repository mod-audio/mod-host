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

# compiler and linker flags
ifeq ($(MODE), release)
   CFLAGS = -O2 -Wall -Wextra
   LDFLAGS = -s
else
   CFLAGS = -O0 -g -Wall -Wextra -DDEBUG
   LDFLAGS =
endif

# library links
LIBS = -ljack -ldl `pkg-config --libs lilv-0` -largtable2 -lreadline -lpthread

# additional include paths
INCS = -I/usr/include/lilv-0 -I../src

# remove command
RM = rm -f

# source and object files
SRC = $(wildcard $(SRC_DIR)/*.$(EXT))
OBJ = $(SRC:.$(EXT)=.o) $(shell ls ../src/*.o | grep -v mod-host.o)

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
