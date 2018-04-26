APP                 = njconnect
CFLAGS              = -O2 -Wall -g
PKG_CONFIG_MODULES := jack ncurses
DESTDIR =

CFLAGS             += $(shell pkg-config --cflags $(PKG_CONFIG_MODULES))
LDFLAGS             =
LIBRARIES           = $(shell pkg-config --libs   $(PKG_CONFIG_MODULES))
OBJS                = njconnect.o jslist_extra.o

.PHONY: all,clean

all: $(APP)

njconnect: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBRARIES) $(LDFLAGS)

clean:
	rm -f $(APP) $(OBJS)

install: all
	install -Dm755 $(APP) $(DESTDIR)/usr/bin/$(APP)
