LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-main-door

acs-main-door: acs-main-door.o ../common/config.o

install-systemd: acs-main-door.service
	cp acs-main-door.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-main-door.service

install: install-systemd acs-main-door
	install -m755 acs-main-door $(DESTDIR)/usr/sbin

clean:
	rm -f acs-main-door acs-main-door.o

.PHONY: all clean install install-systemd enable-systemd
