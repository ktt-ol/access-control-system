LIBS=-lmosquitto
LDFLAGS+=${LIBS}

abus-cfa1000-sensor: abus-cfa1000-sensor.o interface.o ../common/config.o ../common/i2c.o ../common/gpio.o

clean:
	rm -f acs-abus-cfa1000-sensor acs-abus-cfa1000-sensor.o

install:
	install -m755 abus-cfa1000-sensor $(DESTDIR)/usr/sbin/acs-abus-cfa1000-sensor

.PHONY: clean install
