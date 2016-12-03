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
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <arpa/inet.h>

#include "../keyboard/gpio.h"
#include "../common/config.h"
#include "../common/i2c.h"

#define BLACK  0x00000000
#define YELLOW 0x40400000
#define ORANGE 0x80200000
#define GREEN  0x00800000
#define RED    0x80000000
#define RED2   0x40000000
#define PURPLE 0x30002000
#define BLUE   0x00008000
#define CYAN   0x00401500

#define MODE_SET   (0x00 << 6)
#define MODE_FADE  (0x01 << 6)
#define MODE_BLINK (0x02 << 6)
#define MODE_GLOW  (0x03 << 6)

#define TIME_MASK  0x3f

#define BOLT_STATE "/access-control-system/main-door/bolt-state"
#define TOPIC_STATE_CUR "/access-control-system/space-state"
#define TOPIC_STATE_NEXT "/access-control-system/space-state-next"

const static char* states[] = {
	"unknown",
	"disconnected",
	"none",
	"keyholder",
	"member",
	"open",
	"open+",
};

struct gpiodesc modegpio = { "i2c/1-0022", 3, "tiny-ws2812 mode", true, false, -1, -1 }; /* high = i2c, low = led */

// P1.3 = mode pin

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

struct userdata {
	int i2c;
	enum states2 curstate;
	enum states2 nextstate;
	bool bolt;
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

static bool led_get(struct userdata *udata, uint8_t i, uint32_t *val) {
	uint32_t readval;
	int retries, err;
	int fd = udata->i2c;

	for(retries = 0; retries < 3; retries++) {
		err = i2c_smbus_read_i2c_block_data(fd, i, 4, (uint8_t*) &readval);
		if (err == -1)
			continue;
		*val = ntohl(readval);
		return true;
	}

	return false;

}

static bool led_set(struct userdata *udata, uint8_t i, uint32_t val) {
	uint32_t beval = htonl(val);
	int retries, err;
	int fd = udata->i2c;

	for(retries = 0; retries < 5; retries++) {
		err = i2c_smbus_write_i2c_block_data(fd, i, 4, (uint8_t*) &beval);
		if (err == -1)
			continue;
		return true;
	}

	return false;
}

static bool led_check(struct userdata *udata, uint8_t i, uint32_t val) {
	uint32_t beval = htonl(val);
	uint32_t readval;
	int retries, err;
	int fd = udata->i2c;

	for(retries = 0; retries < 5; retries++) {
		err = i2c_smbus_read_i2c_block_data(fd, i, 4, (uint8_t*) &readval);
		if (err == -1)
			continue;
		readval = ntohl(readval);
		if ((val & 0xffffffff) == (readval & 0xffffffff))
			return true;
		led_set(udata, i, val);
		usleep(1000);
	}

	return false;
}

enum location {
	LOCATION_BELL_BUTTON,
	LOCATION_INDOOR,
	LOCATION_KEYPAD,
	LOCATION_BELL_BUTTON_2,
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
		case LOCATION_BELL_BUTTON_2:
			start=7;
			stop=8;
			break;
		case LOCATION_STRIPE:
			start=8;
			stop=32;
			break;
		case LOCATION_ALL:
			start=0;
			stop=32;
			break;
	}

	gpio_write(&modegpio, 1);

	for(i=start; i < stop; i++)
		led_set(udata, i, color);
	for(i=start; i < stop; i++)
		led_check(udata, i, color);

	gpio_write(&modegpio, 0);
}


static void display_state(struct userdata *udata, int curstate, int nextstate) {
	printf("curstate: %s - nextstate: %s\n", states[curstate], states[nextstate]);
	udata->curstate = curstate;
	udata->nextstate = nextstate;

	uint32_t color = BLACK;

	switch(curstate) {
		case STATE_OPEN_PLUS:
			color = CYAN;
			break;
		case STATE_OPEN:
			color = GREEN;
			break;
		case STATE_KEYHOLDER:
			color = PURPLE;
			break;
		case STATE_MEMBER:
			color = YELLOW;
			break;
		case STATE_NONE:
			color = RED;
			break;
		case STATE_UNKNOWN:
		default:
			color = BLUE;
			break;
	}

	switch(nextstate) {
		case STATE_NONE:
		case STATE_KEYHOLDER:
		case STATE_MEMBER:
			/* space is closing for guests */
			color = ORANGE;
		case STATE_OPEN:
		case STATE_OPEN_PLUS:
		case STATE_UNKNOWN:
		default:
			/* ignore and use current state */
			break;
	}

	/* closed + LOCKED -> dark red */
	if (color == RED && udata->bolt)
		color = RED2;

	color |= (MODE_FADE | 63);

	sed_multi_led(udata, LOCATION_ALL, color);
}

static void on_connect(struct mosquitto *m, void *data, int res) {
	int ret;
	struct userdata *udata = (struct userdata*) data;

	fprintf(stderr, "Connected!\n");

	ret = mosquitto_subscribe(m, NULL, BOLT_STATE, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", BOLT_STATE, ret);
		exit(1);
	}


	ret = mosquitto_subscribe(m, NULL, TOPIC_STATE_CUR, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC_STATE_CUR, ret);
		exit(1);
	}

	ret = mosquitto_subscribe(m, NULL, TOPIC_STATE_NEXT, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC_STATE_NEXT, ret);
		exit(1);
	}}

static void on_disconnect(struct mosquitto *m, void *data, int res) {
	struct userdata *udata = (struct userdata*) data;

	/* ignore repeated events */
	if(udata->curstate == STATE_DISCONNECTED)
		return;

	fprintf(stderr, "Disconnected\n");
	display_state(udata, STATE_DISCONNECTED, STATE_UNKNOWN);
}

static enum states2 str2state(char *state, uint32_t len) {
	int curstate = STATE_UNKNOWN;
	int i;

	for(i=0; i < STATE_MAX; i++) {
		if (strlen(states[i]) != len)
			continue;

		if(!strncmp(states[i], state, len)) {
			curstate = i;
			break;
		}
	}

	return curstate;
}

static void on_message(struct mosquitto *m, void *udata, const struct mosquitto_message *msg) {
	/* wrong */
	if(!strcmp(TOPIC_STATE_CUR, msg->topic)) {
		int curstate = str2state(msg->payload, msg->payloadlen);
		display_state(udata, curstate, ((struct userdata *) udata)->nextstate);
		return;
	} else if(!strcmp(TOPIC_STATE_NEXT, msg->topic)) {
		int nextstate = str2state(msg->payload, msg->payloadlen);
		display_state(udata, ((struct userdata *) udata)->curstate, nextstate);
		return;
	} else if (!strcmp(BOLT_STATE, msg->topic) && msg->payloadlen) {
		((struct userdata *) udata)->bolt = ((char*) msg->payload)[0] == '1';
		display_state(udata, ((struct userdata *) udata)->curstate, ((struct userdata *) udata)->nextstate);
		return;
	}

	fprintf(stderr, "Ignored message with wrong topic\n");
	return;
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
	udata->curstate = STATE_UNKNOWN;
	udata->nextstate = STATE_UNKNOWN;

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

	ret = gpio_init(&modegpio);
	if (ret) {
		fprintf(stderr, "Could not open mode gpio: %d\n", ret);
	}

	/* initial state unknown */
	display_state(udata, STATE_UNKNOWN, STATE_UNKNOWN);

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
