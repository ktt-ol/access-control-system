/*
 * Access Control System - GPIO Sensor to MQTT
 *
 * Copyright (c) 2016, Sebastian Reichel <sre@mainframe.io>
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

struct gpio_sensor {
	char *gpio;
	char *topic;
	int fd;
	bool cached_value;
};

struct gpio_sensor sensors[] = {
	{ "glass-door-bell-button",  "/access-control-system/glass-door/bell-button", -1, 0 },
	{ "glass-door-bolt-switch",  "/access-control-system/glass-door/bolt-contact", -1, 0 },
	{ "main-door-bell-button",  "/access-control-system/main-door/bell-button", -1, 0 },
	{ "main-door-reed-switch",  "/access-control-system/main-door/reed-switch", -1, 0 },
	{}
};

#define GPIO_TIMEOUT 5 * 60 * 1000

static void on_connect(struct mosquitto *m, void *udata, int res) {
	fprintf(stderr, "Connected.\n");
}

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
	fprintf(stderr, "Message published.\n");
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	int ret = 0;
	bool cur_gpio = false;
	bool old_gpio_main_door = true, old_gpio_glass_door = false;
	char *state;
	int i;
	struct pollfd *fdset;
	int nfds;

	mosquitto_lib_init();

	FILE *cfg = cfg_open();
	char *user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	char *pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	char *cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
	char *host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
	int port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	int keepalv = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);
	cfg_close(cfg);

	/* create mosquitto client instance */
	mosq = mosquitto_new("acs-gpio-sensor", true, NULL);
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

	for (nfds=0; sensors[nfds].gpio; nfds++);
	fdset = calloc(sizeof(struct pollfd), nfds);
	if (!fdset) {
		fprintf(stderr, "Out of memory!\n");
		return 1;
	}

	for (i=0; sensors[i].gpio; i++) {
		sensors[i].fd = gpio_get(sensors[i].gpio);
		if (sensors[i].fd < 0) {
			fprintf(stderr, "Couldn't get GPIO %s: %d\n", sensors[i].gpio, sensors[i].fd);
			return 1;
		}

		sensors[i].cached_value = !gpio_read(sensors[i].fd);
		fdset[i].fd = sensors[i].fd;
		fdset[i].events = POLLPRI;
	}

	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	for (;;) {
		for (i=0; sensors[i].gpio; i++) {
			bool val = gpio_read(sensors[i].fd);
			if (val == sensors[i].cached_value)
				continue;

			sensors[i].cached_value = val;

			fprintf(stderr, "gpio changed: %s is now %d\n", sensors[i].gpio, val);

			/* publish state */
			ret = mosquitto_publish(mosq, NULL, sensors[i].topic, 1, val ? "1" : "0", 0, true);
			if (ret) {
				fprintf(stderr, "Error could not send message: %d\n", ret);
				return 1;
			}

		}

		ret = poll(fdset, nfds, GPIO_TIMEOUT);
		if(ret < 0) {
				fprintf(stderr, "Failed to poll gpio: %d\n", ret);
				return 1;
		}
	}

	free(fdset);
	free(user);
	free(pass);
	free(cert);
	free(host);

	/* cleanup */
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return 0;
}
