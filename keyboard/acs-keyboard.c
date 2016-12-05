/*
* Access Control System - Keyboard Pin check
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <linux/input.h>
#include <sys/ioctl.h>
#include <mosquitto.h>

#include "../common/config.h"

#define KEY_RELEASE 0
#define KEY_PRESS 1
#define KEY_KEEPING_PRESSED 2

#define BUFFER_SIZE 32

#define TOPIC_BUZZER "/access-control-system/main-door/buzzer"

struct mosquitto *m;

void input(char *code) {
	if (strcmp(code, "4891")) {
		fprintf(stderr, "incorrect code: %s\n", code);
		return;
	}

	fprintf(stderr, "correct code, open main door!\n");
	mosquitto_publish(m, NULL, TOPIC_BUZZER, 2, "1", 0, true);
	alarm(3);

	return;
}

void on_alarm(int signal) {
	fprintf(stderr, "timeout, close main door!\n");
	mosquitto_publish(m, NULL, TOPIC_BUZZER, 2, "0", 0, true);
}

static void on_connect(struct mosquitto *m, void *udata, int res) {
	fprintf(stderr, "Connected.\n");
}

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
	fprintf(stderr, "Message published.\n");
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
	mosq = mosquitto_new("acs-keyboard", true, NULL);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return NULL;
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

	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return NULL;
	}

	return mosq;
}


int main (int argc, char *argv[]) {
	int i, fd, pos, err;
	struct input_event ev[64];
	char buffer[BUFFER_SIZE + 1] = {0};

	if ((fd = open("/dev/input/by-path/platform-gpio-keys-event", O_RDONLY)) < 0) {
		perror("Couldn't open input device");
		return 1;
	}

	mosquitto_lib_init();
	m = mqtt_init();
	if (!m)
		return 1;

	signal(SIGALRM, on_alarm);

	while (1) {
		size_t rb = read(fd, ev, sizeof(ev));

		if (rb < (int) sizeof(struct input_event)) {
			perror("short read");
			return 1;
		}

		for (i = 0; i < (int) (rb / sizeof(struct input_event)); i++) {
		
			if (EV_KEY != ev[i].type)
				continue;
		
			if (KEY_PRESS != ev[i].value)
				continue;

			switch (ev[i].code) {
				case KEY_0:
					buffer[pos] = '0';
					pos++;
					break;
				case KEY_1:
					buffer[pos] = '1';
					pos++;
					break;
				case KEY_2:
					buffer[pos] = '2';
					pos++;
					break;
				case KEY_3:
					buffer[pos] = '3';
					pos++;
					break;
				case KEY_4:
					buffer[pos] = '4';
					pos++;
					break;
				case KEY_5:
					buffer[pos] = '5';
					pos++;
					break;
				case KEY_6:
					buffer[pos] = '6';
					pos++;
					break;
				case KEY_7:
					buffer[pos] = '7';
					pos++;
					break;
				case KEY_8:
					buffer[pos] = '8';
					pos++;
					break;
				case KEY_9:
					buffer[pos] = '9';
					pos++;
					break;
				case KEY_CANCEL:
					bzero(buffer, BUFFER_SIZE);
					pos = 0;
					break;
				case KEY_OK:
					input(buffer);
					bzero(buffer, BUFFER_SIZE);
					pos = 0;
					break;
				default:
					break;
			}

			pos %= BUFFER_SIZE;
		}
	}

	return 0;

}
