/*
 * Space Status Switch LEDs
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
#include <mosquitto.h>
#include <poll.h>
#include <malloc.h>
#include <stdlib.h>
#include "../common/config.h"
#include "i2c.h"

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
	int i2c;
	enum states2 curstate;
	enum states2 laststate;
};

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}


static void on_subscribe(struct mosquitto *m, void *udata, int mid, int qos_count, const int *granted_qos) {
	int i;

	fprintf(stderr, "Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++) {
		fprintf(stderr, ", %d", granted_qos[i]);
	}

	fprintf(stderr, "\n");
}

static void set_led(struct userdata *udata, int id, uint32_t color) {
	i2c_write(udata->i2c, id*3+0x00, (color & 0xff0000) >> 16);
	i2c_write(udata->i2c, id*3+0x01, (color & 0x00ff00) >> 8);
	i2c_write(udata->i2c, id*3+0x02, (color & 0x0000ff) >> 0);
}

enum location {
	LOCATION_BELL_BUTTON,
	LOCATION_INDOOR,
	LOCATION_KEYPAD,
	LOCATION_STRIPE,
	LOCATION_ALL,
	LOCATION_MAX
};

static void sed_multi_led(struct userdata *udata, uint8_t location, uint32_t color) {
	int start=0;
	int stop=0;
	int i;

	if(location >= LOCATION_MAX)
		return;

	switch(location) {
		case LOCATION_BELL_BUTTON:
			start=0;
			stop=1;
			break;
		case LOCATION_INDOOR:
			start=1;
			stop=2;
			break;
		case LOCATION_KEYPAD:
			start=2;
			stop=7;
			break;
		case LOCATION_STRIPE:
			start=7;
			stop=31;
			break;
		case LOCATION_ALL:
			start=0;
			stop=31;
			break;
	}

	for(i=start; i < stop; i++)
		set_led(udata, i, color);
}


static void display_state(struct userdata *udata, int state) {
	printf("state: %s\n", states[state]);
	udata->curstate = state;

	switch(state) {
		case STATE_OPENED:
			sed_multi_led(udata, LOCATION_ALL, 0x008000);
			break;
		case STATE_CLOSING:
			sed_multi_led(udata, LOCATION_ALL, 0x404000);
			break;
		case STATE_CLOSED:
			sed_multi_led(udata, LOCATION_ALL, 0x800000);
			break;
		case STATE_UNKNOWN:
		default:
			sed_multi_led(udata, LOCATION_ALL, 0x000080);
			break;
	}
}

static void on_connect(struct mosquitto *m, void *data, int res) {
	int ret;
	struct userdata *udata = (struct userdata*) data;

	fprintf(stderr, "Connected, laststate=%s.\n", states[udata->laststate]);
	display_state(udata, udata->laststate);

	ret = mosquitto_subscribe(m, NULL, STATE_TOPIC, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", STATE_TOPIC, ret);
		exit(1);
	}
}

static void on_disconnect(struct mosquitto *m, void *data, int res) {
	struct userdata *udata = (struct userdata*) data;

	/* ignore repeated events */
	if(udata->curstate == STATE_DISCONNECTED)
		return;

	udata->laststate = udata->curstate;

	fprintf(stderr, "Disconnected, laststate=%s.\n", states[udata->laststate]);
	display_state(udata, STATE_DISCONNECTED);
}

static void on_message(struct mosquitto *m, void *udata, const struct mosquitto_message *msg) {
	int i;
	int curstate = STATE_UNKNOWN;

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

	display_state(udata, curstate);
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	struct userdata *udata;
	int ret = 0;

	mosquitto_lib_init();

	FILE *cfg = cfg_open();
	char *user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	char *pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	char *cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
	char *host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
	int port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	int keepalv = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);
	int i2c_busid = cfg_get_int_default(cfg, "i2c-leds-bus", I2C_LEDS_BUS);
	int i2c_devid = cfg_get_int_default(cfg, "i2c-leds-dev", I2C_LEDS_DEV);
	cfg_close(cfg);

	udata = malloc(sizeof(*udata));
	if(!udata) {
		printf("out of memory!\n");
		return 1;
	}
	udata->laststate = STATE_UNKNOWN;
	udata->curstate = STATE_UNKNOWN;

	/* create mosquitto client instance */
	mosq = mosquitto_new("space-status-leds", true, udata);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	udata->i2c = i2c_open(i2c_busid, i2c_devid);
	if (udata->i2c < 0) {
		fprintf(stderr, "Could not open I2C: %d\n", udata->i2c);
		return 1;
	}

	/* initial state unknown */
	display_state(udata, STATE_UNKNOWN);

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);
	mosquitto_log_callback_set(mosq, on_log);
	mosquitto_subscribe_callback_set(mosq, on_subscribe);
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

	ret = mosquitto_loop_forever(mosq, -1, 1);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	free(user);
	free(pass);
	free(cert);
	free(host);

	free(udata);

	return 0;
}
