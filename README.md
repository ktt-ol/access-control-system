=== Building ===

 * apt-get install build-essential debhelper dh-systemd libmosquitto-dev libpcre3-dev libsqlite3-dev libssl-dev
 * dpkg-buildpackage -b

=== Installation ===

 * sudo dpkg -i access-control-system_0.0.1_$(dpkg-architecture -q DEB_HOST_ARCH).deb

=== Configuration ===

 * sudo vim /etc/access-control-system.conf
