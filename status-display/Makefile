all: acs-status-display

acs-status-display: acs-status-display.o

install-systemd: acs-status-display.service
	cp acs-status-display.service /lib/systemd/system
	systemctl daemon-reload
	systemctl enable acs-status-display.service

install: acs-status-display install-systemd
	install -m755 acs-status-display /usr/sbin

clean:
	rm -f acs-status-display acs-status-display.o

.PHONY: all clean install install-systemd
