LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-leds

acs-leds: acs-leds.o ../common/i2c.o ../keyboard/gpio.o ../common/config.o

led-test: led-test.o

install: install-systemd acs-leds
	install -m755 acs-leds $(DESTDIR)/usr/sbin

install-systemd:
	cp acs-leds.service $(DESTDIR)/lib/systemd/system
	systemctl daemon-reload

clean:
	rm -f acs-leds acs-leds.o

.PHONY: all clean install
