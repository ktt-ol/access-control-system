all:
	cd status-display && make
	cd switch && make

clean:
	cd status-display && make clean
	cd switch && make clean

install:
	cd status-display && make install
	cd switch && make install
	install -m 644 data/access-control-system.conf $(DESTDIR)/etc

.PHONY: all clean install
