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

#define ABUS_CFA1000_GPIO_IRQ 42
#define ABUS_CFA1000_I2C_BUS 1
#define ABUS_CFA1000_I2C_DEV 1

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
