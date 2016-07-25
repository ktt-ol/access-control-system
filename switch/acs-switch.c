/*
 * Space Status Switch
 *
 * Copyright (c) 2015, Sebastian Reichel <sre@mainframe.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <mosquitto.h>
#include <poll.h>
#include <stdlib.h>
#include "../common/config.h"
#include "../common/gpio.h"

#define STATE_TOPIC "/access-control-system/space-state"
#define GPIO_TIMEOUT 60 * 1000

const static char* states[] = {
	"opened",
	"closing",
	"closed",
	"unknown",
};

static void on_connect(struct mosquitto *m, void *udata, int res) {
	fprintf(stderr, "Connected.\n");
}

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
	fprintf(stderr, "Message published.\n");
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}

static const unsigned char gpios_merge(bool gpio1, bool gpio2) {
	return (gpio1 << 1 | gpio2);
}

static const char* gpios_decode(unsigned char gpios) {
	switch(gpios) {
		case 0x00:
			return states[1];
		case 0x01:
			return states[2];
		case 0x02:
			return states[0];
		default:
			return states[3];
	}
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	int ret = 0;
	unsigned char old_gpios = 0xFF;

	mosquitto_lib_init();

	FILE *cfg = cfg_open();
	char *user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	char *pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	char *cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
	char *host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
	int port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	int keepalv = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);
	int gpiot = cfg_get_int_default(cfg, "gpio-switch-top", GPIO_SWITCH_TOP);
	int gpiob = cfg_get_int_default(cfg, "gpio-switch-bottom", GPIO_SWITCH_BOTTOM);
	cfg_close(cfg);

	/* create mosquitto client instance */
	mosq = mosquitto_new("space-status-switch", true, NULL);
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

	/* read switch state */
	int switch_top = gpio_open(gpiot, false, false);
	if(switch_top == -1) {
		fprintf(stderr, "could not open gpio\n");
		return 1;
	}

	int switch_bottom = gpio_open(gpiob, false, false);
	if(switch_bottom == -1) {
		fprintf(stderr, "could not open gpio\n");
		return 1;
	}

	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	for (;;) {
		struct pollfd fdset[2];
		int nfds = 2;

		fdset[0].fd = switch_top;
		fdset[0].events = POLLPRI;
		fdset[1].fd = switch_bottom;
		fdset[1].events = POLLPRI;

		ret = poll(fdset, nfds, GPIO_TIMEOUT);
		if(ret < 0) {
				fprintf(stderr, "Failed to poll gpios: %d\n", ret);
				return 1;
		}

		unsigned char gpios = gpios_merge(gpio_read(switch_top), gpio_read(switch_bottom));

		if(gpios != old_gpios) {
			old_gpios = gpios;
			const char *state = gpios_decode(gpios);

			fprintf(stderr, "new state: %s\n", state);

			/* publish state */
			ret = mosquitto_publish(mosq, NULL, STATE_TOPIC, strlen(state), state, 0, true);
			if (ret) {
				fprintf(stderr, "Error could not send message: %d\n", ret);
				return 1;
			}
		} else {
			fprintf(stdout, "gpios: 0x%02x\n", gpios);
		}
	}

	free(user);
	free(pass);
	free(cert);
	free(host);

	/* cleanup */
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return 0;
}
