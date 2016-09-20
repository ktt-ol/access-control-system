LIBS=-lmosquitto
LDFLAGS+=${LIBS}

acs-mqtt-fwd: acs-mqtt-fwd.o ../common/config.o
acs-mqtt-fwd.o: acs-mqtt-fwd.c ../common/config.h
../common/config.o: ../common/config.c ../common/config.h

clean:
	rm -f acs-mqtt-fwd acs-mqtt-fwd.o ../common/config.o

install:
	install -m755 acs-mqtt-fwd $(DESTDIR)/usr/bin/

install-systemd:
	cp acs-mqtt-fwd.service $(DESTDIR)/lib/systemd/system

enable-systemd:
	systemctl daemon-reload
	systemctl enable acs-mqtt-fwd.service

.PHONY: clean install
