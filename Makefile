# compiler
CC ?= gcc

# language file extension
EXT = c

# source files directory
SRC_DIR = ./src

# program name
PROG = mod-host

# default install paths
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
SHAREDIR = $(PREFIX)/share
MANDIR = $(SHAREDIR)/man/man1/

# default compiler and linker flags
CFLAGS += -O3 -Wall -Wextra -c -std=gnu99
LDFLAGS += -s

# debug mode compiler and linker flags
ifeq ($(DEBUG), 1)
   CFLAGS = -O0 -g -Wall -Wextra -c -DDEBUG -std=gnu99
   LDFLAGS =
endif

# libraries
LIBS = $(shell pkg-config --libs jack lilv-0) -lreadline -lpthread

# include paths
INCS = $(shell pkg-config --cflags jack lilv-0)

ifeq ($(shell pkg-config --atleast-version=3.3.5 fftw3 fftw3f && echo true), true)
LIBS += $(shell pkg-config --libs fftw3 fftw3f) -lfftw3_threads -lfftw3f_threads
INCS += $(shell pkg-config --cflags fftw3 fftw3f) -DHAVE_FFTW335
endif


# remove command
RM = rm -f

# source and object files
SRC = $(wildcard $(SRC_DIR)/*.$(EXT))
OBJ = $(SRC:.$(EXT)=.o)

# linking rule
$(PROG): get_info $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) $(LIBS) -o $(PROG)
	@rm src/info.h

# meta-rule to generate the object files
%.o: %.$(EXT)
	$(CC) $(INCS) $(CFLAGS) -o $@ $<

# install rule
install: install_man
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(PROG) $(DESTDIR)$(BINDIR)

# clean rule
clean:
	$(RM) $(SRC_DIR)/*.o $(PROG) src/info.h

test:
	py.test tests/test_host.py
	
# manual page rule
# Uses md2man to convert the README to groff man page
# https://github.com/sunaku/md2man
man:
	md2man-roff README.md > doc/mod-host.1

# install manual page rule
install_man:
	install -d $(DESTDIR)$(MANDIR)
	install -m 644 doc/*.1 $(DESTDIR)$(MANDIR)

# generate the source file with the help message
A=`grep -n 'The commands supported' README.md | cut -d':' -f1`
B=`grep -n 'bye!' README.md | cut -d':' -f1`
get_info:
	@sed -n -e "$A,$B p" -e "$B q" README.md > help_msg
	@utils/txt2cvar.py help_msg > src/info.h
	@rm help_msg
	@echo "const char version[] = {\""`git describe --tags`\""};" >> src/info.h
