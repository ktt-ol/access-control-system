/*
 * Space Main Door Control
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

#define TOPIC_BELL_BUTTON "/access-control-system/main-door/bell-button"
#define TOPIC_REED_SWITCH "/access-control-system/main-door/reed-switch"
#define TOPIC_BUZZER "/access-control-system/main-door/buzzer"
#define TOPIC_BELL "/access-control-system/bell"
#define TOPIC_STATE "/access-control-system/space-state"

const static char* states[] = {
	"unknown",
	"disconnected",
	"none",
	"keyholder",
	"member",
	"open",
	"open+",
};

/* order should match states[] */
enum states2 {
	STATE_UNKNOWN,
	STATE_DISCONNECTED,
	STATE_NONE,
	STATE_KEYHOLDER,
	STATE_MEMBER,
	STATE_OPEN,
	STATE_OPEN_PLUS,
	STATE_MAX,
};

enum event {
	EVENT_NONE,
	EVENT_BELL,
	EVENT_BUZZER,
	EVENT_MAX,
};

struct userdata {
	struct mosquitto *mosq;
	enum event eventinprogress;
	bool cached_reed_state;
	enum states2 state;
};

#define GPIO_TIMEOUT 60 * 1000

struct userdata *globaludata;

static void on_connect(struct mosquitto *m, void *udata, int res) {
	int ret;

	fprintf(stderr, "Connected.\n");

	ret = mosquitto_subscribe(m, NULL, TOPIC_REED_SWITCH, 1);
	if (ret) {
		fprintf(stderr, "MQTT Error: Could not subscribe to %s: %d\n", TOPIC_REED_SWITCH, ret);
		exit(1);
	}

	ret = mosquitto_subscribe(m, NULL, TOPIC_BELL_BUTTON, 1);
	if (ret) {
		fprintf(stderr, "MQTT Error: Could not subscribe to %s: %d\n", TOPIC_BELL_BUTTON, ret);
		exit(1);
	}

	ret = mosquitto_subscribe(m, NULL, TOPIC_STATE, 1);
	if (ret) {
		fprintf(stderr, "MQTT Error: Could not subscribe to %s: %d\n", TOPIC_STATE, ret);
		exit(1);
	}
}

static void on_disconnect(struct mosquitto *m, void *data, int res) {
	struct userdata *udata = (struct userdata*) data;

	fprintf(stderr, "MQTT Disconnected.\n");

	exit(1);
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

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
	fprintf(stderr, "Message published.\n");
}

static void on_reed_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	struct userdata *udata = (struct userdata*) data;
	bool state;

	/* wrong */
	if(strcmp(TOPIC_REED_SWITCH, msg->topic)) {
		fprintf(stderr, "Ignored message with wrong topic\n");
		return;
	}

	if(msg->payloadlen < 1) {
		fprintf(stderr, "Empty payload\n");
		return;
	}

	state = (((char*) msg->payload)[0] == '1');

	fprintf(stderr, "MQTT reed change: %d\n", state);
	udata->cached_reed_state = state;
}

static void on_button_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	struct userdata *udata = (struct userdata*) data;

	if(strncmp("1", msg->payload, msg->payloadlen)) {
		fprintf(stderr, "Bell button no longer pressed!\n");
		return;
	}

	fprintf(stderr, "Bell button pressed!\n");

	if(!udata->cached_reed_state) {
		fprintf(stderr, "button pressed event skipped (door is currently open)!\n");
		return;
	}

	if(udata->eventinprogress != EVENT_NONE) {
		fprintf(stderr, "button pressed event skipped (already in progress)!\n");
		return;
	}

	if (udata->state == STATE_OPEN_PLUS) {
		/* trigger buzzer */
		udata->eventinprogress = EVENT_BUZZER;
		mosquitto_publish(m, NULL, TOPIC_BUZZER, 2, "1", 0, true);
		alarm(3);
	} else {
		/* ring the bell */
		udata->eventinprogress = EVENT_BELL;
		mosquitto_publish(m, NULL, TOPIC_BELL, 2, "1", 0, true);
		alarm(1);
	}
}

static void on_state_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	int i;
	enum states2 curstate = STATE_UNKNOWN;
	struct userdata *udata = (struct userdata*) data;

	/* wrong */
	if(strcmp(TOPIC_STATE, msg->topic)) {
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

static void on_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	/* status change */
	if(!strcmp(TOPIC_REED_SWITCH, msg->topic)) {
		on_reed_message(m, data, msg);
		return;
	}

	/* bell event */
	if(!strcmp(TOPIC_BELL_BUTTON, msg->topic)) {
		on_button_message(m, data, msg);
		return;
	}

	/* state event */
	if(!strcmp(TOPIC_STATE, msg->topic)) {
		on_state_message(m, data, msg);
		return;
	}


	fprintf(stderr, "Ignored message with wrong topic\n");
	return;
}

void on_alarm(int signal) {
	switch (globaludata->eventinprogress) {
		case EVENT_BELL:
			mosquitto_publish(globaludata->mosq, NULL, TOPIC_BELL, 2, "0", 0, true);
			break;
		case EVENT_BUZZER:
			mosquitto_publish(globaludata->mosq, NULL, TOPIC_BUZZER, 2, "0", 0, true);
			break;
	}

	globaludata->eventinprogress = EVENT_NONE;
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	struct userdata udata;
	int ret = 0;
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
	mosq = mosquitto_new("main-door", true, &udata);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	udata.mosq = mosq;

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);
	mosquitto_publish_callback_set(mosq, on_publish);
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

	/* init udata */
	udata.eventinprogress = false;

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
