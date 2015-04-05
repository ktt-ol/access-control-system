LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-switch acs-leds

acs-switch: acs-switch.o gpio.o

acs-leds: acs-leds.o gpio.o

install-systemd: acs-leds.service acs-switch.service
	cp acs-leds.service /lib/systemd/system
	cp acs-switch.service /lib/systemd/system
	systemctl daemon-reload
	systemctl enable acs-leds.service
	systemctl enable acs-switch.service

install: install-systemd acs-switch acs-leds
	install -m755 acs-leds /usr/sbin
	install -m755 acs-switch /usr/sbin

clean:
	rm -f acs-leds acs-switch acs-leds.o acs-switch.o gpio.o

.PHONY: all clean install install-systemd
