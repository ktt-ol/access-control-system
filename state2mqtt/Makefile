LDFLAGS += -lmosquitto

acs-state2mqtt: acs-state2mqtt.o ../common/config.o ../common/state.o

clean:
	rm -f acs-state2mqtt acs-state2mqtt.o

install:
	install -m755 acs-state2mqtt $(DESTDIR)/usr/sbin/acs-state2mqtt

.PHONY: clean install
