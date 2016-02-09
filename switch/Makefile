LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-switch acs-leds

acs-switch: acs-switch.o ../common/config.o ../common/gpio.o

acs-leds: acs-leds.o  ../common/config.o ../common/gpio.o

install-systemd: acs-leds.service acs-switch.service
	cp acs-leds.service $(DESTDIR)/lib/systemd/system
	cp acs-switch.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-leds.service
	systemctl enable acs-switch.service

install: install-systemd acs-switch acs-leds
	install -m755 acs-leds $(DESTDIR)/usr/sbin
	install -m755 acs-switch $(DESTDIR)/usr/sbin

clean:
	rm -f acs-leds acs-switch acs-leds.o acs-switch.o

.PHONY: all clean install install-systemd enable-systemd
