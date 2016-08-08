LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-leds

acs-leds: acs-leds.o ../common/i2c.o ../common/config.o

install: install-systemd acs-leds
	install -m755 acs-leds $(DESTDIR)/usr/sbin

clean:
	rm -f acs-leds acs-leds.o

.PHONY: all clean install
