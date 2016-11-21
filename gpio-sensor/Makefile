LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-gpio-sensor

acs-gpio-sensor: acs-gpio-sensor.o ../common/config.o

install-systemd: acs-gpio-sensor.service
	cp acs-gpio-sensor.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-gpio-sensor.service

install: install-systemd acs-gpio-sensor
	install -m755 acs-gpio-sensor $(DESTDIR)/usr/sbin

clean:
	rm -f acs-gpio-sensor acs-gpio-sensor.o

.PHONY: all clean install install-systemd enable-systemd
