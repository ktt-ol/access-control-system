LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: acs-button

acs-button: acs-button.o ../common/config.o ../common/gpio.o

install-systemd: acs-button.service
	cp acs-button.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-button.service

install: install-systemd acs-button
	install -m755 acs-button $(DESTDIR)/usr/sbin

clean:
	rm -f acs-button acs-button.o

.PHONY: all clean install install-systemd enable-systemd
