abus-cfa1000: abus-cfa1000.o ../common/config.o
abus-cfa1000.o: abus-cfa1000.c ../common/config.h
../common/config.o: ../common/config.c ../common/config.h

clean:
	rm -f acs-abus-cfa1000 abus-cfa1000.o ../common/config.o

install:
	install -m755 abus-cfa1000 $(DESTDIR)/usr/sbin/acs-abus-cfa1000

.PHONY: clean install
