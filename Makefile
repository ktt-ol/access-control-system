all:
	cd abus-cfa1000 && make
	cd keyholder-interface && make
	cd mqtt-fwd && make
	cd status-display && make
	cd switch && make

clean:
	cd abus-cfa1000 && make clean
	cd keyholder-interface && make clean
	cd mqtt-fwd && make clean
	cd status-display && make clean
	cd switch && make clean

install:
	cd abus-cfa1000 && make install
	cd keyholder-interface && make install
	cd mqtt-fwd && make install
	cd status-display && make install
	cd switch && make install
	install -m 644 data/access-control-system.conf $(DESTDIR)/etc

.PHONY: all clean install
