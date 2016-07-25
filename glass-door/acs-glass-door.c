/*
 * Space Glass Door Control
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
#include <signal.h>
#include "../common/config.h"
#include "../common/gpio.h"

#define BELL_TOPIC "/access-control-system/glass-door/bell-button"
#define STATE_TOPIC "/access-control-system/space-state"
const static char* states[] = {
	"unknown",
	"disconnected",
	"opened",
	"closing",
	"closed",
};

/* order should match states[] */
enum states2 {
	STATE_UNKNOWN,
	STATE_DISCONNECTED,
	STATE_OPENED,
	STATE_CLOSING,
	STATE_CLOSED,
	STATE_MAX,
};

struct userdata {
	struct mosquitto *mosq;
	enum states2 state;
	bool eventinprogress;
	int buzzer;
	int bell;
};

#define GPIO_TIMEOUT 60 * 1000

struct userdata *globaludata;

static void on_connect(struct mosquitto *m, void *udata, int res) {
	int ret;

	fprintf(stderr, "Connected.\n");

	ret = mosquitto_subscribe(m, NULL, STATE_TOPIC, 1);
	if (ret) {
		fprintf(stderr, "MQTT Error: Could not subscribe to %s: %d\n", STATE_TOPIC, ret);
		exit(1);
	}

	ret = mosquitto_subscribe(m, NULL, BELL_TOPIC, 1);
	if (ret) {
		fprintf(stderr, "MQTT Error: Could not subscribe to %s: %d\n", STATE_TOPIC, ret);
		exit(1);
	}
}

static void on_disconnect(struct mosquitto *m, void *data, int res) {
	struct userdata *udata = (struct userdata*) data;

	/* ignore repeated events */
	if(udata->state == STATE_DISCONNECTED)
		return;

	udata->state = STATE_DISCONNECTED;

	fprintf(stderr, "MQTT Disconnected.\n");
}

static void on_subscribe(struct mosquitto *m, void *udata, int mid, int qos_count, const int *granted_qos) {
	int i;

	fprintf(stderr, "MQTT: Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++) {
		fprintf(stderr, ", %d", granted_qos[i]);
	}

	fprintf(stderr, "\n");
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}

static void on_state_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	int i;
	enum states2 curstate = STATE_UNKNOWN;
	struct userdata *udata = (struct userdata*) data;

	/* wrong */
	if(strcmp(STATE_TOPIC, msg->topic)) {
		fprintf(stderr, "Ignored message with wrong topic\n");
		return;
	}

	for(i=0; i < STATE_MAX; i++) {
		if(!strncmp(states[i], msg->payload, msg->payloadlen)) {
			curstate = i;
			break;
		}
	}

	if(curstate == STATE_UNKNOWN) {
		char *m = strndup(msg->payload, msg->payloadlen);
		fprintf(stderr, "Incorrect state received: %s\n", m);
		free(m);
	}

	fprintf(stderr, "MQTT state change: %s\n", states[curstate]);

	udata->state = curstate;
}

static void on_button_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	struct userdata *udata = (struct userdata*) data;
	bool new_gpio_state;

	if(strncmp("1", msg->payload, msg->payloadlen)) {
		fprintf(stderr, "Bell button no longer pressed!\n");
		return;
	}

	fprintf(stderr, "Bell button pressed!\n");

	if(udata->eventinprogress)
		return;

	udata->eventinprogress = true;

	if(udata->state == STATE_OPENED) {
		gpio_write(udata->buzzer, true);
		alarm(5);
	} else if(udata->state == STATE_CLOSING) {
		gpio_write(udata->buzzer, true);
		gpio_write(udata->bell, true);
		alarm(1);
	} else {
		gpio_write(udata->bell, true);
		alarm(1);
	}
}

static void on_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	/* status change */
	if(!strcmp(STATE_TOPIC, msg->topic)) {
		on_state_message(m, data, msg);
		return;
	}

	/* bell event */
	if(!strcmp(BELL_TOPIC, msg->topic)) {
		on_button_message(m, data, msg);
		return;
	}

	fprintf(stderr, "Ignored message with wrong topic\n");
	return;
}

void on_alarm(int signal) {
	/* disable buzzer and bell */
	gpio_write(globaludata->buzzer, false);
	gpio_write(globaludata->bell, false);
	globaludata->eventinprogress = false;
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	struct userdata udata;
	int ret = 0;
	bool cur_gpio = false;
	bool old_gpio = false;
	char *state;

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
	mosq = mosquitto_new("glass-door", true, &udata);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);
	mosquitto_subscribe_callback_set(mosq, on_subscribe);
	mosquitto_log_callback_set(mosq, on_log);
	mosquitto_message_callback_set(mosq, on_message);

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

	/* open gpios */
	udata.buzzer = gpio_get("glass-door-buzzer");
	if(udata.buzzer == -1) {
		fprintf(stderr, "could not open buzzer gpio\n");
		return 1;
	}
	gpio_write(udata.buzzer, false);

	udata.bell = gpio_get("bell");
	if(udata.bell == -1) {
		fprintf(stderr, "could not open bell gpio\n");
		return 1;
	}
	gpio_write(udata.bell, false);

	globaludata = &udata;
	signal(SIGALRM, on_alarm);

	ret = mosquitto_loop_forever(mosq, -1, 1);
	if (ret) {
		fprintf(stderr, "Error could not setup mosquitto network loop: %d\n", ret);
		return 1;
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
