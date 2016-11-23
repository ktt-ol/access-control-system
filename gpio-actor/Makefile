LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-gpio-actor

acs-gpio-actor: acs-gpio-actor.o ../common/config.o ../keyboard/gpio.o

install-systemd: acs-gpio-actor.service
	cp acs-gpio-actor.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-gpio-actor.service

install: install-systemd acs-gpio-actor
	install -m755 acs-gpio-actor $(DESTDIR)/usr/sbin

clean:
	rm -f acs-gpio-actor acs-gpio-actor.o

.PHONY: all clean install install-systemd enable-systemd
