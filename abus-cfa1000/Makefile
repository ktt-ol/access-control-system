LIBS=-lmosquitto
LDFLAGS+=${LIBS}

all: abus-cfa1000-setup abus-cfa1000-sensor

abus-cfa1000-sensor: abus-cfa1000-sensor.o interface.o ../common/config.o ../common/i2c.o ../keyboard/gpio.o
abus-cfa1000-setup: abus-cfa1000-setup.o interface.o ../common/config.o ../common/i2c.o ../keyboard/gpio.o

clean:
	rm -f acs-abus-cfa1000-sensor acs-abus-cfa1000-sensor.o
	rm -f acs-abus-cfa1000-setup acs-abus-cfa1000-setup.o
	rm -f interface.o

install:
	install -m755 abus-cfa1000-sensor $(DESTDIR)/usr/sbin/acs-abus-cfa1000-sensor
	install -m755 abus-cfa1000-setup $(DESTDIR)/usr/sbin/acs-abus-cfa1000-setup

.PHONY: clean install
