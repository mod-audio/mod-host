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
LIBDIR = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include
SHAREDIR = $(PREFIX)/share
MANDIR = $(SHAREDIR)/man/man1/

# default compiler and linker flags
CFLAGS += -O3 -Wall -Wextra -c -std=gnu99 -fPIC -D_GNU_SOURCE -pthread
CFLAGS += -Wno-deprecated-declarations
CFLAGS += -Werror=implicit-function-declaration -Werror=return-type

# debug mode compiler and linker flags
ifeq ($(DEBUG), 1)
   CFLAGS += -O0 -g -Wall -Wextra -c -DDEBUG
   LDFLAGS +=
else
   CFLAGS += -fvisibility=hidden
   LDFLAGS += -s
endif

ifeq ($(TESTBUILD), 1)
# CFLAGS += -Wconversion -Wsign-conversion -Wdouble-promotion
CFLAGS += -Werror -Wabi=98 -Wcast-qual -Wclobbered -Wdisabled-optimization
CFLAGS += -Wfloat-equal -Wlogical-op -Wpointer-arith
CFLAGS += -Wformat=2 -Woverlength-strings
# CFLAGS += -Wformat-truncation=2 -Wformat-overflow=2
CFLAGS += -Wstringop-overflow=4 -Wstringop-truncation
CFLAGS += -Wmissing-declarations -Wredundant-decls
CFLAGS += -Wshadow  -Wundef -Wuninitialized -Wunused
CFLAGS += -Wstrict-aliasing -fstrict-aliasing
CFLAGS += -Wstrict-overflow -fstrict-overflow
CFLAGS += -Wduplicated-branches -Wduplicated-cond -Wnull-dereference
CFLAGS += -Winit-self -Wjump-misses-init -Wmissing-prototypes -Wnested-externs -Wstrict-prototypes -Wwrite-strings
endif

# libraries
LIBS = $(shell pkg-config --libs lilv-0)

ifeq ($(MOD_DESKTOP),1)
LIBS += $(subst -ljack ,-ljackserver ,$(shell pkg-config --libs jack))
ifneq ($(MACOS)$(WINDOWS),true)
LIBS += -Wl,-rpath,'$$ORIGIN/..'
endif
else
LIBS += $(shell pkg-config --libs jack)
endif

# include paths
INCS = $(shell pkg-config --cflags jack lilv-0)

ifneq ($(SKIP_FFTW335), 1)
ifeq ($(shell pkg-config --atleast-version=3.3.5 fftw3 fftw3f && echo true), true)
LIBS += $(shell pkg-config --libs-only-L fftw3 fftw3f) -lfftw3_threads -lfftw3f_threads $(shell pkg-config --libs fftw3 fftw3f)
INCS += $(shell pkg-config --cflags fftw3 fftw3f) -DHAVE_FFTW335
endif
endif

ifeq ($(SKIP_READLINE), 1)
INCS += -DSKIP_READLINE
else
LIBS += -lreadline
endif

ifeq ($(shell pkg-config --atleast-version=0.22.0 lilv-0 && echo true), true)
INCS += -DHAVE_NEW_LILV
endif

ifeq ($(shell pkg-config --atleast-version=1.18 lv2 && echo true),true)
INCS += -DHAVE_LV2_STATE_FREE_PATH
endif

ifeq ($(shell pkg-config --atleast-version=1.9.0 jack && echo true), true)
INCS += -DHAVE_JACK2
endif

ifeq ($(HAVE_NE10),true)
LIBS += -lNE10
INCS += -DHAVE_NE10
endif

# control chain support
ifeq ($(shell pkg-config --atleast-version=0.7.0 cc_client && echo true), true)
LIBS += $(shell pkg-config --libs cc_client)
INCS += $(shell pkg-config --cflags cc_client) -DHAVE_CONTROLCHAIN
endif

# hylia/link support
ifeq ($(shell pkg-config --exists hylia && echo true), true)
LIBS += $(shell pkg-config --libs hylia)
INCS += $(shell pkg-config --cflags hylia) -DHAVE_HYLIA
endif

LIBS += -lpthread -lm

# incompatible flags
MACHINE = $(shell $(CC) -dumpmachine)
ifneq (,$(findstring mingw,$(MACHINE)))
LIBS += -liphlpapi -lws2_32
endif

ifeq (,$(findstring apple,$(MACHINE)))
LDFLAGS += -Wl,--no-undefined
ifeq (,$(findstring mingw,$(MACHINE)))
LIBS += -lrt
endif
endif

# source and object files
SRC  = $(wildcard $(SRC_DIR)/*.$(EXT))
SRC += $(SRC_DIR)/dsp/compressor_core.c
SRC += $(SRC_DIR)/monitor/monitor-client.c
SRC += $(SRC_DIR)/sha1/sha1.c
SRC += $(SRC_DIR)/rtmempool/rtmempool.c
OBJ  = $(SRC:.$(EXT)=.o)

# default build
all: $(PROG) $(PROG).so fake-input.so mod-monitor.so

# linking rule
$(PROG): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) $(LIBS) -o $@

$(PROG).so: $(OBJ)
ifeq ($(MODAPP),1)
	$(CC) $(OBJ) $(LDFLAGS) $(subst -ljack ,-ljackserver ,$(LIBS)) -shared -o $@
else
	$(CC) $(OBJ) $(LDFLAGS) $(LIBS) -shared -o $@
endif

# meta-rule to generate the object files
%.o: %.$(EXT) src/info.h
	$(CC) $(INCS) $(CFLAGS) -o $@ $<

# custom rules for fake-input client
fake-input.so: src/fake-input.o
	$(CC) $< $(LDFLAGS) $(LIBS) -shared -o $@

src/fake-input.o: src/fake-input/fake-input.c
	$(CC) $(INCS) $(CFLAGS) -o $@ $<

# custom rules for monitor client
mod-monitor.so: src/mod-monitor.o src/dsp/compressor_core.o
	$(CC) $^ $(LDFLAGS) $(LIBS) -shared -o $@

src/mod-monitor.o: src/monitor/monitor-client.c
	$(CC) $(INCS) $(CFLAGS) -DSTANDALONE_MONITOR_CLIENT -o $@ $<

# install rule
install: install_man
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(PROG) $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/
	install -m 644 $(PROG).so $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/
	install -m 644 fake-input.so $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/
	install -m 644 mod-monitor.so $(DESTDIR)$(shell pkg-config --variable=libdir jack)/jack/

# clean rule
clean:
	@rm -f $(SRC_DIR)/*.o $(SRC_DIR)/*/*.o $(PROG) $(PROG).exe $(PROG).so fake-input.so mod-monitor.so src/info.h tests/protocol_test tests/host_scenarios \
	      $(PROTOCOL_LIB) $(PROTOCOL_SONAME) $(PROTOCOL_DEVLINK)

test:
	py.test tests/test_host.py

# the socket protocol and command dispatch, tested against a fake backend: no jack, no plugins
PROTOCOL_OBJ = $(SRC_DIR)/socket.o $(SRC_DIR)/protocol.o $(SRC_DIR)/utils.o $(SRC_DIR)/host-dispatch.o \
               $(SRC_DIR)/host-scenario.o

test-protocol: tests/protocol_test
	./tests/protocol_test

tests/protocol_test: tests/protocol_test.c $(PROTOCOL_OBJ)
	$(CC) $(INCS) $(filter-out -c,$(CFLAGS)) -Werror -o $@ $< $(PROTOCOL_OBJ) -lpthread -lm

tests/host_scenarios: tests/host_scenarios.c $(PROTOCOL_OBJ)
	$(CC) $(INCS) $(filter-out -c,$(CFLAGS)) -Werror -o $@ $< $(PROTOCOL_OBJ) -lpthread -lm

# the same objects as a shared library, for another host that speaks this protocol.
# Opt-in: `make lib` and `make install-lib`; mod-host itself does not link it.
# The soname is bumped on any ABI change while the version is 0.x, a field appended to
# host_backend_t included.
PROTOCOL_VERSION = 0.1.0
PROTOCOL_SOVERSION = 0
PROTOCOL_DEVLINK = libmod-host-protocol.so
PROTOCOL_SONAME = $(PROTOCOL_DEVLINK).$(PROTOCOL_SOVERSION)
PROTOCOL_LIB = $(PROTOCOL_DEVLINK).$(PROTOCOL_VERSION)
PROTOCOL_LIB_OBJ = $(PROTOCOL_OBJ:.o=.lib.o)
PROTOCOL_HDR = $(SRC_DIR)/host-backend.h $(SRC_DIR)/host-dispatch.h $(SRC_DIR)/host-errors.h $(SRC_DIR)/mod-host.h \
               $(SRC_DIR)/host-scenario.h $(SRC_DIR)/protocol.h $(SRC_DIR)/socket.h $(SRC_DIR)/utils.h
PROTOCOL_PC_LIBDIR = $(patsubst $(PREFIX)/%,$${prefix}/%,$(LIBDIR))
PROTOCOL_PC_INCLUDEDIR = $(patsubst $(PREFIX)/%,$${prefix}/%,$(INCLUDEDIR))
PROTOCOL_PC_SHAREDIR = $(patsubst $(PREFIX)/%,$${prefix}/%,$(SHAREDIR))

lib: $(PROTOCOL_DEVLINK)

$(PROTOCOL_LIB): $(PROTOCOL_LIB_OBJ)
	$(CC) $^ $(LDFLAGS) -shared -Wl,-soname,$(PROTOCOL_SONAME) -lpthread -lm -o $@

$(PROTOCOL_DEVLINK): $(PROTOCOL_LIB)
	ln -sf $(PROTOCOL_LIB) $(PROTOCOL_SONAME)
	ln -sf $(PROTOCOL_SONAME) $@

# the library's objects keep their symbols visible
$(SRC_DIR)/%.lib.o: $(SRC_DIR)/%.$(EXT) src/info.h
	$(CC) $(INCS) $(CFLAGS) -fvisibility=default -o $@ $<

install-lib: $(PROTOCOL_LIB)
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig
	install -m 755 $(PROTOCOL_LIB) $(DESTDIR)$(LIBDIR)
	ln -sf $(PROTOCOL_LIB) $(DESTDIR)$(LIBDIR)/$(PROTOCOL_SONAME)
	ln -sf $(PROTOCOL_SONAME) $(DESTDIR)$(LIBDIR)/$(PROTOCOL_DEVLINK)
	install -d $(DESTDIR)$(INCLUDEDIR)/mod-host
	install -m 644 $(PROTOCOL_HDR) $(DESTDIR)$(INCLUDEDIR)/mod-host
	install -d $(DESTDIR)$(SHAREDIR)/mod-host
	install -m 644 tests/host-scenarios.txt $(DESTDIR)$(SHAREDIR)/mod-host
	sed -e 's,@PREFIX@,$(PREFIX),' -e 's,@LIBDIR@,$(PROTOCOL_PC_LIBDIR),' \
	    -e 's,@INCLUDEDIR@,$(PROTOCOL_PC_INCLUDEDIR),' -e 's,@SHAREDIR@,$(PROTOCOL_PC_SHAREDIR),' \
	    -e 's,@VERSION@,$(PROTOCOL_VERSION),' \
	    mod-host-protocol.pc.in > $(DESTDIR)$(LIBDIR)/pkgconfig/mod-host-protocol.pc

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
