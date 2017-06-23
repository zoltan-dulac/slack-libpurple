LIBNAME = slack_plugin.so

.PHONY: all
all: $(LIBNAME)

C_SRCS = cstring.c json.c miniwebsock.c slack_connection.c slack_chat.c slack_plugin.c #slack_messages.c

# Object file names using 'Substitution Reference'
C_OBJS = $(C_SRCS:.c=.o)

CC = gcc
LD = $(CC)

PURPLE_MOD ?= $(shell pkg-config --exists purple-3 && echo purple-3 || echo purple)
PLUGIN_DIR_PURPLE:=$(shell pkg-config --variable=plugindir $(PURPLE_MOD))
DATA_ROOT_DIR_PURPLE:=$(shell pkg-config --variable=datarootdir $(PURPLE_MOD))
PKGS=$(PURPLE_MOD) libcurl

CFLAGS = \
    -g \
    -O2 \
    -Wall \
    -Wextra \
    -fPIC \
    -DPURPLE_PLUGINS \
    -DPIC -DENABLE_NLS \
    -std=c99 \
    $(shell pkg-config --cflags $(PKGS))

LIBS = $(shell pkg-config --libs $(PKGS))

LDFLAGS = -shared

%.o: %.c
	$(V_CC)$(CC) -c $(CFLAGS) -o $@ $<

$(LIBNAME): $(C_OBJS)
	$(V_LINK)$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: install install-user
install: $(LIBNAME)
	install -D $(LIBNAME) $(PLUGIN_DIR_PURPLE)/$(LIBNAME)
	install --mode=0644 img/slack16.png $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/16/slack.png
	install --mode=0644 img/slack22.png $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/22/slack.png
	install --mode=0644 img/slack48.png $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/48/slack.png

install-user: $(LIBNAME)
	install -D $(LIBNAME) $(HOME)/.purple/plugins/$(LIBNAME)

.PHONY: uninstall
uninstall: $(LIBNAME)
	rm $(PLUGIN_DIR_PURPLE)/$(LIBNAME)
	rm $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/16/slack.png
	rm $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/22/slack.png
	rm $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/48/slack.png

.PHONY: clean
clean:
	-rm *.o
	-rm $(LIBNAME)


# Quiet by default
VERBOSE ?= 0

# Define printf macro
v_printf = @printf "  %-8s%s\n"

# Define C verbose macro
V_CC = $(v_CC_$(V))
v_CC_ = $(v_CC_$(VERBOSE))
v_CC_0 = $(v_printf) CC $(@F);

# Define LINK verbose macro
V_LINK = $(v_LINK_$(V))
v_LINK_ = $(v_LINK_$(VERBOSE))
v_LINK_0 = $(v_printf) LINK $(@F);
