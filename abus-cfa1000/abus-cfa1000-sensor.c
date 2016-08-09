#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <mosquitto.h>
#include "../common/config.h"
#include "../common/i2c.h"
#include "../common/gpio.h"
#include "interface.h"

int dev;
int irq;

#define TOPIC "/access-control-system/main-door/bolt-state"

#define GPIO_TIMEOUT 5 * 60 * 1000

#define MCP23017_INTCON_A 0x08
#define MCP23017_INTCON_B 0x09
#define MCP23017_GPINTEN_A 0x04
#define MCP23017_GPINTEN_B 0x05
#define MCP23017_IOCON_A 0x0a
#define MCP23017_IOCON_B 0x0b

static void on_connect(struct mosquitto *m, void *udata, int res) {
	fprintf(stderr, "Connected.\n");
}

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
	fprintf(stdout, "Message published.\n");
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}

static int publish_state(struct mosquitto *mosq, enum lock_state state) {
	int ret;
	char *mqtt_state = "-1";
	if(state == LOCK_STATE_LOCKED)
		mqtt_state = "1";
	else if(state == LOCK_STATE_UNLOCKED)
		mqtt_state = "0";

	ret = mosquitto_publish(mosq, NULL, TOPIC, strlen(mqtt_state), mqtt_state, 0, true);
	if (ret) {
		fprintf(stderr, "Error could not send message: %d\n", ret);
		return 1;
	}

	return 0;
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	int ret;

	mosquitto_lib_init();

	FILE *cfg = cfg_open();
	int i2c_busid = cfg_get_int_default(cfg, "abus-cfa1000-i2c-bus", -1);
	int i2c_devid = cfg_get_int_default(cfg, "abus-cfa1000-i2c-dev", -1);

	char *user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	char *pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	char *cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
	char *host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
	int port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	int keepalv = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);
	cfg_close(cfg);

	if (i2c_busid < 0 || i2c_devid < 0) {
		fprintf(stderr, "Please configure busid and devid!\n");
		return 1;
	}

	dev = i2c_open(i2c_busid, i2c_devid);
	if (dev < 0) {
		fprintf(stderr, "Could not open I2C device: %d\n", dev);
		return 1;
	}

	irq = gpio_get("abus-cfa1000-irq");
	if (irq < 0) {
		fprintf(stderr, "Could not open IRQ GPIO: %d\n", irq);
		return 1;
	}

	/* create mosquitto client instance */
	mosq = mosquitto_new("acs-abus-cfa1000-sensor", true, NULL);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_publish_callback_set(mosq, on_publish);
	mosquitto_log_callback_set(mosq, on_log);

	/* setup credentials */
	if (strcmp(user, "")) {
		ret = mosquitto_username_pw_set(mosq, user, pass);
		if(ret) {
			fprintf(stderr, "Error setting credentials: %d\n", ret);
			return 1;
		}
	}

	if (strcmp(cert, "")) {
		ret = mosquitto_tls_set(mosq, cert, NULL, NULL, NULL, NULL);
		if(ret) {
			fprintf(stderr, "Error setting TLS mode: %d\n", ret);
			return 1;
		}

		ret = mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
		if(ret) {
			fprintf(stderr, "Error requiring TLS 1.2: %d\n", ret);
			return 1;
		}
	}

	/* connect to broker */
	ret = mosquitto_connect(mosq, host, port, keepalv);
	if (ret) {
		fprintf(stderr, "Error could not connect to broker: %d\n", ret);
		return 1;
	}

	/* mqtt background handling */
	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	/* irq: compare against previous value */
	i2c_write16(dev, MCP23017_INTCON_A, 0x0000);

	/* irq: check all bits */
	i2c_write16(dev, MCP23017_GPINTEN_A, 0xFFFF);

	/* irq: enable mirror mode (IRQs are OR'd) */
	i2c_write16(dev, MCP23017_IOCON_A, 0x7070);

	struct pollfd fdset;
	fdset.fd = irq;
	fdset.events = POLLPRI;

	struct display_data_t olddisp = { .symbol = '\0', .state = LOCK_STATE_UNKNOWN };
	for (;;) {
		char irqstate = gpio_read(irq);
		struct display_data_t disp = display_read(dev);
		char *state = lock_state_str(disp.state);

		if (olddisp.symbol != disp.symbol || olddisp.state != disp.state)
			fprintf(stderr, "state=%s (disp=%c) [irq=%d]\n", state, disp.symbol, irqstate);

		if (olddisp.state != disp.state)
			publish_state(mosq, disp.state);

		olddisp = disp;

		ret = poll(&fdset, 1, GPIO_TIMEOUT);
		if(ret < 0) {
				fprintf(stderr, "Failed to poll gpio: %d\n", ret);
				return 1;
		}
	}

	i2c_close(dev);
	close(irq);


	return 0;
}
