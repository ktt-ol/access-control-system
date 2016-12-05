LDFLAGS += -lmosquitto

acs-doorctrl: acs-doorctrl.o ../common/config.o ../keyboard/gpio.o ../common/state.o

clean:
	rm -f acs-doorctrl acs-doorctrl.o

install:
	install -m755 acs-doorctrl $(DESTDIR)/usr/sbin/acs-doorctrl

install-systemd:
	cp acs-doorctrl.service $(DESTDIR)/lib/systemd/system
	systemctl daemon-reload

.PHONY: clean install
