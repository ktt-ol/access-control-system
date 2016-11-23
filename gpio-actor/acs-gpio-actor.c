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

#define __STDC_FORMAT_MACROS
#define _GNU_SOURCE
#include <linux/gpio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <malloc.h>
#include <inttypes.h>
#include <dirent.h>
#include <poll.h>
#include <mosquitto.h>
#include "../keyboard/gpio.h"
#include "../common/config.h"

struct mqttgpio {
	char *topic;
	struct gpiodesc desc;
};

struct mqttgpio gpios[] = {
	{"/access-control-system/main-door/buzzer",		{ "i2c/1-0021", 0, "maindoor buzzer", true, true, -1, -1 }},
	{"/access-control-system/glass-door/buzzer",	{ "i2c/1-0022", 4, "glassdoor buzzer", true, true, -1, -1 }},
	{"/access-control-system/bell",					{ "i2c/1-0022", 0, "bell", true, true, -1, -1 }},
	{}
};

static void on_connect(struct mosquitto *m, void *udata, int res) {
	fprintf(stderr, "Connected.\n");
}

static void on_message(struct mosquitto *m, void *udata, const struct mosquitto_message *msg) {
	int i;

	for (i = 0; gpios[i].desc.dev; i++) {
		if(strcmp(gpios[i].topic, msg->topic))
			continue;

		bool val = (msg->payloadlen == 0 || ((char*) msg->payload)[0] == '0') ? false : true;

		fprintf(stderr, "Set GPIO %s: %d\n", gpios[i].desc.name, val);
		gpio_write(&gpios[i].desc, val);

		break;
	}
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}

struct mosquitto* mqtt_init() {
	struct mosquitto *mosq;
	int ret;

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
	mosq = mosquitto_new("acs-gpio-actor", true, NULL);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return NULL;
	}

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_message_callback_set(mosq, on_message);
	mosquitto_log_callback_set(mosq, on_log);

	/* setup credentials */
	if (strcmp(user, "")) {
		ret = mosquitto_username_pw_set(mosq, user, pass);
		if(ret) {
			fprintf(stderr, "Error setting credentials: %d\n", ret);
			return NULL;
		}
	}

	if (strcmp(cert, "")) {
		ret = mosquitto_tls_set(mosq, cert, NULL, NULL, NULL, NULL);
		if(ret) {
			fprintf(stderr, "Error setting TLS mode: %d\n", ret);
			return NULL;
		}

		ret = mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
		if(ret) {
			fprintf(stderr, "Error requiring TLS 1.2: %d\n", ret);
			return NULL;
		}
	}

	/* connect to broker */
	ret = mosquitto_connect(mosq, host, port, keepalv);
	if (ret) {
		fprintf(stderr, "Error could not connect to broker: %d\n", ret);
		return NULL;
	}

	return mosq;
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	int i;
	int err;

	/* setup gpios */
	for (i = 0; gpios[i].desc.dev; i++) {
		int err = gpio_init(&gpios[i].desc);
		if (err) {
			fprintf(stderr, "could not init gpio \"%s\": %d!\n", gpios[i].desc.name, err);
			return 1;
		}
		gpio_write(&gpios[i].desc, 0);
	}

	/* init mqtt */
	mosq = mqtt_init();
	if (!mosq)
		return 1;

	/* subscribe */
	for (i = 0; gpios[i].desc.dev; i++) {
		err = mosquitto_subscribe(mosq, NULL, gpios[i].topic, 1);
		if (err) {
			fprintf(stderr, "could not subscribe to mqtt \"%s\": %d!\n", gpios[i].topic, err);
			return 1;
		}
	}

	/* loop */
	err = mosquitto_loop_forever(mosq, -1, 1);
	if (err) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", err);
		return 1;
	}


	return 1;
};
