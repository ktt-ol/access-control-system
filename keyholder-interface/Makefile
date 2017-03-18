LIBS=`pkg-config --libs libcrypto libpcre sqlite3 libsystemd`
LDFLAGS+=${LIBS} -lreadline
CFLAGS+=`pkg-config --cflags libcrypto libpcre sqlite3 libsystemd` -Wall --std=gnu99

acs: acs.o ../common/config.o
acs.o: acs.c ../common/config.h
../common/config.o: ../common/config.c ../common/config.h

clean:
	rm -f acs acs.o ../common/config.o

install:
	install -m755 -o root -g root acs $(DESTDIR)/usr/bin
	chmod u+s $(DESTDIR)/usr/bin/acs

setup:
	useradd -d /home/keyholder -s /usr/bin/acs -U keyholder
	rm -rf /home/keyholder
	mkdir /home/keyholder
	chown root:root /home/keyholder
	chmod 755 /home/keyholder
	mkdir /home/keyholder/.ssh
	chown root:root /home/keyholder/.ssh
	chmod 755 /home/keyholder/.ssh
	touch /home/keyholder/.ssh/authorized_keys
	chown root:keyholder /home/keyholder/.ssh/authorized_keys
	chmod 640 /home/keyholder/.ssh/authorized_keys

.PHONY: clean install setup
