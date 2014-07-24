# compiler
CC = gcc

# linker
LD = gcc

# language file extension
EXT = c

# source files directory
SRC_DIR = ./src

# program name
PROG = mod-host

# default install paths
PREFIX = /usr/local
INSTALL_PATH = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1/

# default compiler and linker flags
CFLAGS = -O3 -Wall -Wextra -c -std=gnu99
LDFLAGS = -s

# debug mode compiler and linker flags
ifeq ($(DEBUG), 1)
   CFLAGS = -O0 -g -Wall -Wextra -c -DDEBUG
   LDFLAGS =
endif

# library links
LIBS = -ljack `pkg-config --libs lilv-0` -largtable2 -lreadline -lpthread

# additional include paths
INCS = -I/usr/include/lilv-0

# remove command
RM = rm -f

# source and object files
SRC = $(wildcard $(SRC_DIR)/*.$(EXT))
OBJ = $(SRC:.$(EXT)=.o)

# linking rule
$(PROG): $(OBJ)
	$(LD) $(LDFLAGS) $(OBJ) -o $(PROG) $(LIBS)

# meta-rule to generate the object files
%.o: %.$(EXT)
	$(CC) $(CFLAGS) $(INCS) -o $@ $<

# install rule
install:
	install $(PROG) $(INSTALL_PATH)

# clean rule
clean:
	$(RM) $(SRC_DIR)/*.o $(PROG)

# manual page rule
man:
	txt2man -s 1 -t MOD-HOST doc/man.txt > doc/mod-host.1

# install manual page rule
install-man: man
	install doc/*.1 $(MANDIR)
