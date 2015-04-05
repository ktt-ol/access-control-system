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
#include "gpio.h"
#include "config.h"

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
	int gpio_led_opened;
	int gpio_led_closing;
	int gpio_led_closed;
	enum states2 curstate;
	enum states2 laststate;
};

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] ", level);
	fprintf(stdout, str);
	fprintf(stdout, "\n");
}


static void on_subscribe(struct mosquitto *m, void *udata, int mid, int qos_count, const int *granted_qos) {
	int i;

	fprintf(stderr, "Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++){
		fprintf(stderr, ", %d", granted_qos[i]);
	}

	fprintf(stderr, "\n");
}

static void display_state(struct userdata *udata, int state) {
	printf("state: %s\n", states[state]);
	udata->curstate = state;

	switch(state) {
		case STATE_OPENED:
			gpio_write(udata->gpio_led_opened, true);
			gpio_write(udata->gpio_led_closing, false);
			gpio_write(udata->gpio_led_closed, false);
			break;
		case STATE_CLOSING:
			gpio_write(udata->gpio_led_opened, false);
			gpio_write(udata->gpio_led_closing, true);
			gpio_write(udata->gpio_led_closed, false);
			break;
		case STATE_CLOSED:
			gpio_write(udata->gpio_led_opened, false);
			gpio_write(udata->gpio_led_closing, false);
			gpio_write(udata->gpio_led_closed, true);
			break;
		case STATE_UNKNOWN:
		default:
			gpio_write(udata->gpio_led_opened, true);
			gpio_write(udata->gpio_led_closing, true);
			gpio_write(udata->gpio_led_closed, true);
			break;
	}
}

static void on_connect(struct mosquitto *m, void *data, int res) {
	int ret;
	struct userdata *udata = (struct userdata*) data;

	fprintf(stderr, "Connected, laststate=%s.\n", states[udata->laststate]);
	display_state(udata, udata->laststate);

	ret = mosquitto_subscribe(m, NULL, TOPIC, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC, ret);
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
	if(strcmp(TOPIC, msg->topic)) {
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

	udata->gpio_led_opened = gpio_open(GPIO_LED_OPENED, true);
	if(udata->gpio_led_opened == -1) {
		fprintf(stderr, "could not open gpio %d\n", GPIO_LED_OPENED);
		return 1;
	}

	udata->gpio_led_closing = gpio_open(GPIO_LED_CLOSING, true);
	if(udata->gpio_led_closing == -1) {
		fprintf(stderr, "could not open gpio %d\n", GPIO_LED_CLOSING);
		return 1;
	}

	udata->gpio_led_closed = gpio_open(GPIO_LED_CLOSED, true);
	if(udata->gpio_led_closed == -1) {
		fprintf(stderr, "could not open gpio %d\n", GPIO_LED_CLOSED);
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
	ret = mosquitto_username_pw_set(mosq, USERNAME, PASSWORD);
	if(ret) {
		fprintf(stderr, "Error setting credentials: %d\n", ret);
		return 1;
	}

	ret = mosquitto_tls_set(mosq, SERVER_CERT, NULL, NULL, NULL, NULL);
	if(ret) {
		fprintf(stderr, "Error setting TLS mode: %d\n", ret);
		return 1;
	}

	ret = mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
	if(ret) {
		fprintf(stderr, "Error requiring TLS 1.2: %d\n", ret);
		return 1;
	}

	/* connect to broker */
	ret = mosquitto_connect(mosq, BROKER_HOSTNAME, BROKER_PORT, KEEPALIVE_SECONDS);
	if (ret) {
		fprintf(stderr, "Error could not connect to broker: %d\n", ret);
		return 1;
	}

	ret = mosquitto_loop_forever(mosq, -1, 1);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	free(udata);

	return 0;
}
