all:
	cd keyholder-interface && make
	cd status-display && make
	cd switch && make

clean:
	cd keyholder-interface && make clean
	cd status-display && make clean
	cd switch && make clean

install:
	cd keyholder-interface && make install
	cd status-display && make install
	cd switch && make install
	install -m 644 data/access-control-system.conf $(DESTDIR)/etc

.PHONY: all clean install
