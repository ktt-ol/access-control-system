LIBS=`pkg-config --libs libcrypto libpcre sqlite3`
LDFLAGS+=${LIBS}
CFLAGS+=`pkg-config --cflags libcrypto libpcre sqlite3`

acs: acs.o ../common/config.o
acs.o: acs.c ../common/config.h
../common/config.o: ../common/config.c ../common/config.h

clean:
	rm -f acs acs.o ../common/config.o

install:
	install -m755 acs $(DESTDIR)/usr/bin

.PHONY: clean install
