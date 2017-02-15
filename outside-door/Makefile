LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-outside-door

acs-outside-door: acs-outside-door.o ../common/config.o

install-systemd: acs-outside-door.service
	cp acs-outside-door.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-outside-door.service

install: install-systemd acs-outside-door
	install -m755 acs-outside-door $(DESTDIR)/usr/sbin

clean:
	rm -f acs-outside-door acs-outside-door.o

.PHONY: all clean install install-systemd enable-systemd
