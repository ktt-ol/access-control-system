#ifndef __CONFIG_H
#define __CONFIG_H

#if defined(__GNUC__)
#define __maybe_unused __attribute__((__unused__))
#else
#define __maybe_unused
#endif

#include "gpio.h"

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

/* ----- gpios ----- */

#ifdef CONFIG_SYSFS_GPIO
struct gpio_desc {
	char *name;
	int gpio;
	bool active_low;
	enum gpio_direction direction;
};

static struct gpio_desc __maybe_unused gpios[] = {
	{ "status-switch-top", 27, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "status-switch-bottom", 22, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "glass-door-bolt-switch", 4, ACTIVE_LOW, GPIO_DIRECTION_INPUT },
	{ "glass-door-bell-button", 17, ACTIVE_LOW, GPIO_DIRECTION_INPUT },
	{ "glass-door-buzzer", 18, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
	{ "main-door-bell-button", 23, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "main-door-buzzer", 24, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
	{ "main-door-reed-switch", 25, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "bell", 15, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
	{ "abus-cfa1000-button-lock", 11, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
	{ "abus-cfa1000-button-unlock", 8, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
	{ "abus-cfa1000-button-setup", 10, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
	{ "abus-cfa1000-irq", 9, ACTIVE_LOW, GPIO_DIRECTION_INPUT },
};
#endif

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
