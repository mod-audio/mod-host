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

ifeq ($(TESTBUILD), 1)
# CFLAGS += -Wconversion -Wsign-conversion
CFLAGS += -Werror -Wabi -Wcast-qual -Wclobbered -Wdisabled-optimization -Wfloat-equal -Wformat=2
CFLAGS += -Winit-self -Wmissing-declarations -Woverlength-strings -Wpointer-arith -Wredundant-decls -Wshadow
CFLAGS += -Wundef -Wuninitialized -Wunused -Wnested-externs
CFLAGS += -Wstrict-aliasing -fstrict-aliasing -Wstrict-overflow -fstrict-overflow
CFLAGS += -Wmissing-prototypes -Wstrict-prototypes -Wwrite-strings
endif

# libraries
LIBS = $(shell pkg-config --libs jack lilv-0) -lreadline -lpthread -lrt -lm

# include paths
INCS = $(shell pkg-config --cflags jack lilv-0)

ifneq ($(SKIP_FFTW335), 1)
ifeq ($(shell pkg-config --atleast-version=3.3.5 fftw3 fftw3f && echo true), true)
LIBS += $(shell pkg-config --libs fftw3 fftw3f) -lfftw3_threads -lfftw3f_threads
INCS += $(shell pkg-config --cflags fftw3 fftw3f) -DHAVE_FFTW335
endif
endif

ifeq ($(shell pkg-config --atleast-version=0.22.0 lilv-0 && echo true), true)
INCS += -DHAVE_NEW_LILV
endif

ifeq ($(HAVE_NE10),true)
LIBS += -lNE10
INCS += -DHAVE_NE10
endif

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

# source and object files
SRC = $(wildcard $(SRC_DIR)/*.$(EXT)) $(SRC_DIR)/sha1/sha1.c $(SRC_DIR)/rtmempool/rtmempool.c
OBJ = $(SRC:.$(EXT)=.o)

# default build
all: $(PROG) $(PROG).so mod-monitor.so

# linking rule
$(PROG): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) $(LIBS) -o $@

$(PROG).so: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) $(LIBS) -shared -o $@

# meta-rule to generate the object files
%.o: %.$(EXT) src/info.h
	$(CC) $(INCS) $(CFLAGS) -o $@ $<

# custom rules for monitor client
mod-monitor.so: src/mod-monitor.o
	$(CC) $< $(LDFLAGS) $(LIBS) -shared -o $@

src/mod-monitor.o: src/monitor/monitor-client.c
	$(CC) $(INCS) $(CFLAGS) -o $@ $<

# install rule
install: install_man
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(PROG) $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/
	install -m 755 $(PROG).so $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/
	install -m 755 mod-monitor.so $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/

# clean rule
clean:
	@rm -f $(SRC_DIR)/*.o $(SRC_DIR)/*/*.o $(PROG) $(PROG).so mod-monitor.so src/info.h

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
src/info.h:
	@sed -n -e "$A,$B p" -e "$B q" README.md > help_msg
	@utils/txt2cvar.py help_msg > src/info.h
	@rm help_msg
	@echo "const char version[] = {\""`git describe --tags 2>/dev/null || echo 0.0.0`\""};" >> src/info.h
