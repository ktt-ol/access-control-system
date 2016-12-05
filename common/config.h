#ifndef __CONFIG_H
#define __CONFIG_H

#if defined(__GNUC__)
#define __maybe_unused __attribute__((__unused__))
#else
#define __maybe_unused
#endif

#define CONFIGFILE "/etc/access-control-system.conf"

/* ----- defaults for variables from configfile ----- */

#define SSHLOGFILE "/var/log/auth.log"
#define SSHKEYFILE "/home/keyholder/.ssh/authorized_keys"
#define DATABASE "/var/lib/acs.db"
#define STATEDIR "/run/acs-state/"

#define MQTT_BROKER_EXTERNAL_HOST "mainframe.io"
#define MQTT_BROKER_EXTERNAL_PORT 8883
#define MQTT_BROKER_EXTERNAL_CERT "mainframe.io-mqtt.crt"

#define MQTT_BROKER_HOST "spacegate.mainframe.lan"
#define MQTT_BROKER_PORT 8883
#define MQTT_BROKER_CERT "spacegate.crt"

#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_KEEPALIVE_SECONDS 60

#define I2C_LEDS_BUS 1
#define I2C_LEDS_DEV 0x23

#define ABUS_CFA1000_I2C_BUS 1
#define ABUS_CFA1000_I2C_DEV 0x20

#define NETWORK_DEV "eth0"
#define SERIAL_DISPLAY_DEV "/dev/ttyUSB0"

/* ----- config file interface ----- */

FILE *cfg_open();
void cfg_close(FILE *cfg);
char *cfg_get(FILE *cfg, char *key);
static inline char *cfg_get_default(FILE *cfg, char *key, char *def) {
	char *result = cfg_get(cfg, key);
	return result ? result : def;
}
int cfg_get_int(FILE *cfg, char *key);
static inline int cfg_get_int_default(FILE *cfg, char *key, int def) {
	int result = cfg_get_int(cfg, key);
	return (result >= 0) ? result : def;
}

#endif
