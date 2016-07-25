#ifndef __CONFIG_H
#define __CONFIG_H

#include "gpio.h"

#define CONFIGFILE "/etc/access-control-system.conf"

/* ----- defaults for variables from configfile ----- */

#define SSHLOGFILE "/var/log/auth.log"
#define SSHKEYFILE "/home/keyholder/.ssh/authorized_keys"
#define DATABASE "/var/lib/access-control-system"
#define STATEDIR "/run/access-control-system/"

#define MQTT_BROKER_HOST "localhost"
#define MQTT_BROKER_PORT 8883
#define MQTT_BROKER_CERT ""
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_KEEPALIVE_SECONDS 60

#define GPIO_LED_OPENED 25
#define GPIO_LED_CLOSING 24
#define GPIO_LED_CLOSED 23
#define GPIO_SWITCH_TOP 27
#define GPIO_SWITCH_BOTTOM 22
#define GPIO_SWITCH_BELL 17
#define GPIO_SWITCH_BOLT_CONTACT 4
#define GPIO_BUZZER 18
#define GPIO_BELL 15

#define I2C_LEDS_BUS 1
#define I2C_LEDS_DEV 0x23

#define ABUS_CFA1000_GPIO_IRQ 42
#define ABUS_CFA1000_I2C_BUS 1
#define ABUS_CFA1000_I2C_DEV 1

#define NETWORK_DEV "eth0"
#define SERIAL_DISPLAY_DEV "/dev/ttyUSB0"

/* ----- gpios ----- */

struct gpio_desc {
	char *name;
	int gpio;
	bool active_low;
	enum gpio_direction direction;
};

static struct gpio_desc gpios[] = {
	{ "status-switch-top", 27, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "status-switch-bottom", 22, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "glass-door-bolt-switch", 4, ACTIVE_LOW, GPIO_DIRECTION_INPUT },
	{ "glass-door-bell-button", 17, ACTIVE_LOW, GPIO_DIRECTION_INPUT },
	{ "glass-door-buzzer", 18, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
	{ "main-door-bell-button", 23, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "main-door-buzzer", 24, ACTIVE_HIGH, GPIO_DIRECTION_OUTPUT },
	{ "main-door-reed-switch", 25, ACTIVE_HIGH, GPIO_DIRECTION_INPUT },
	{ "bell", 15, ACTIVE_LOW, GPIO_DIRECTION_OUTPUT },
};

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
