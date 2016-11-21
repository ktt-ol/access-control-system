all: acs-keyboard

acs-keyboard: acs-keyboard.o gpio.o

install-systemd: acs-keyboard.service
	cp acs-keyboard.service $(DESTDIR)/lib/systemd/system

enable-systemd: install-systemd
	systemctl daemon-reload
	systemctl enable acs-keyboard.service

install: install-systemd acs-keyboard
	install -m755 acs-keyboard $(DESTDIR)/usr/sbin

clean:
	rm -f acs-keyboard acs-keyboard.o gpio.o

.PHONY: all clean install install-systemd enable-systemd
