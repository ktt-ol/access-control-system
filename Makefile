all:
	cd abus-cfa1000 && make
	cd keyholder-interface && make
	cd mqtt-fwd && make
	cd status-display && make
	cd switch && make
	cd i2c-led && make
	cd button && make
	cd glass-door && make
	cd main-door && make
	cd gpio-sensor && make

clean:
	cd abus-cfa1000 && make clean
	cd keyholder-interface && make clean
	cd mqtt-fwd && make clean
	cd status-display && make clean
	cd switch && make clean
	cd i2c-led && make clean
	cd button && make clean
	cd glass-door && make clean
	cd main-door && make clean
	cd gpio-sensor && make clean
	rm -f common/config.o common/gpio.o

install:
	cd abus-cfa1000 && make install
	cd keyholder-interface && make install
	cd mqtt-fwd && make install
	cd status-display && make install
	cd switch && make install
	cd i2c-led && make install
	cd button && make install
	cd glass-door && make install
	cd main-door && make install
	cd gpio-sensor && make install
	install -m 644 data/access-control-system.conf $(DESTDIR)/etc

.PHONY: all clean install
