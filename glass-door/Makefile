LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-glass-door

acs-glass-door: acs-glass-door.o ../common/config.o

install-systemd: acs-glass-door.service
	cp acs-glass-door.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-glass-door.service

install: install-systemd acs-glass-door
	install -m755 acs-glass-door $(DESTDIR)/usr/sbin

clean:
	rm -f acs-glass-door acs-glass-door.o

.PHONY: all clean install install-systemd enable-systemd
