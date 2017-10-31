LIBNAME = libslack.so

.PHONY: all
all: $(LIBNAME)

C_SRCS = slack.c \
	 slack-message.c \
	 slack-channel.c \
	 slack-im.c \
	 slack-user.c \
	 slack-rtm.c \
	 slack-blist.c \
	 slack-api.c \
	 slack-object.c \
	 slack-json.c \
	 purple-websocket.c

# Object file names using 'Substitution Reference'
C_OBJS = $(C_SRCS:.c=.o)

CC = gcc
LD = $(CC)

PURPLE_MOD=purple
PLUGIN_DIR_PURPLE:=$(DESTDIR)$(shell pkg-config --variable=plugindir $(PURPLE_MOD))
DATA_ROOT_DIR_PURPLE:=$(DESTDIR)$(shell pkg-config --variable=datarootdir $(PURPLE_MOD))
PKGS=$(PURPLE_MOD) glib-2.0 gobject-2.0 json-parser

CFLAGS = \
    -g \
    -O2 \
    -Wall -Werror \
    -fPIC \
    -D_DEFAULT_SOURCE=1 \
    -std=c99 \
    $(shell pkg-config --cflags $(PKGS))

LIBS = $(shell pkg-config --libs $(PKGS))

LDFLAGS = -shared

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<
%.E: %.c
	$(CC) -E $(CFLAGS) -o $@ $<

$(LIBNAME): $(C_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: install install-user
install: $(LIBNAME)
	install -d $(PLUGIN_DIR_PURPLE) $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/{16,22,48}
	install $(LIBNAME) $(PLUGIN_DIR_PURPLE)/$(LIBNAME)
	install --mode=0644 img/slack16.png $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/16/slack.png
	install --mode=0644 img/slack22.png $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/22/slack.png
	install --mode=0644 img/slack48.png $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/48/slack.png

install-user: $(LIBNAME)
	install -d $(HOME)/.purple/plugins
	install $(LIBNAME) $(HOME)/.purple/plugins/$(LIBNAME)

.PHONY: uninstall
uninstall: $(LIBNAME)
	rm $(PLUGIN_DIR_PURPLE)/$(LIBNAME)
	rm $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/16/slack.png
	rm $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/22/slack.png
	rm $(DATA_ROOT_DIR_PURPLE)/pixmaps/pidgin/protocols/48/slack.png

.PHONY: clean
clean:
	rm -f *.o $(LIBNAME) Makefile.dep

Makefile.dep: $(C_SRCS)
	pkg-config --modversion $(PKGS)
	$(CC) -MM $(CFLAGS) $^ > Makefile.dep
include Makefile.dep
