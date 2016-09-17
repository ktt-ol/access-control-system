LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-switch

acs-switch: acs-switch.o ../common/config.o ../common/gpio.o

install-systemd: acs-switch.service
	cp acs-switch.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-switch.service

install: install-systemd acs-switch
	install -m755 acs-switch $(DESTDIR)/usr/sbin

clean:
	rm -f acs-switch acs-switch.o

.PHONY: all clean install install-systemd enable-systemd
