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

/* for asprintf */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <mosquitto.h>
#include <poll.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <arpa/inet.h>
#include <sys/inotify.h>
#include <pthread.h>

#include "../keyboard/gpio.h"
#include "../common/config.h"
#include "../common/i2c.h"

#define BLACK  0x00000000
#define YELLOW 0x40400000
#define ORANGE 0x80400000
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

#define TOPIC_BOLT_STATE "/access-control-system/main-door/bolt-state"
#define TOPIC_MAIN_DOOR_BUZZER "/access-control-system/main-door/buzzer"
#define TOPIC_GLASS_DOOR_BUZZER "/access-control-system/glass-door/buzzer"
#define TOPIC_STATE_CUR "/access-control-system/space-state"
#define TOPIC_STATE_NEXT "/access-control-system/space-state-next"

/* check all 10 minutes even witout inotify event */
#define POLL_TIMEOUT 10 * 60 * 1000

int ifd; /* inotify file descriptor */
int wfd; /* directory watch file descriptor */

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

enum bus {
	BUS_UNKNOWN,
	LOCAL,
	INTERNAL,
	EXTERNAL
};

/* high = i2c mode, low = led mode ; led -> i2c mode switch needs 1ms */
struct gpiodesc modegpio = { "platform/gpio-sc18is600", 0, "tiny-ws2812 mode", true, false, -1, -1 };

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct userdata {
	int i2c;

	/* local files */
	enum states2 curstate_local;
	enum states2 nextstate_local;

	/* internal mqtt */
	struct mosquitto *mqtt_internal;
	enum states2 curstate_internal;
	enum states2 nextstate_internal;

	/* external mqtt */
	struct mosquitto *mqtt_external;
	enum states2 curstate_external;
	enum states2 nextstate_external;

	/* true if main door is locked */
	bool bolt;

	/* true if main-door buzzer is active */
	bool buzzer_maindoor;

	/* true if glass-door buzzer is active */
	bool buzzer_glassdoor;
};
struct userdata *globaldata;

enum bus mqtt_to_bus_id(struct mosquitto *m, struct userdata *udata) {
	if (m == udata->mqtt_external)
		return EXTERNAL;
	else if(m == udata->mqtt_internal)
		return INTERNAL;
	else
		return BUS_UNKNOWN;
}


const char *mqtt_to_bus_str(struct mosquitto *m, struct userdata *udata) {
	if (m == udata->mqtt_external)
		return "external";
	else if(m == udata->mqtt_internal)
		return "internal";
	else
		return "unknown";
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	const char *bus = mqtt_to_bus_str(m, udata);
	fprintf(stdout, "[%s %d] %s\n", bus, level, str);
}

static void on_subscribe(struct mosquitto *m, void *udata, int mid, int qos_count, const int *granted_qos) {
	const char *bus = mqtt_to_bus_str(m, udata);
	int i;

	fprintf(stderr, "%s: Subscribed (mid: %d): %d", bus, mid, granted_qos[0]);
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
	LOCATION_BELL_BUTTON_GLASS,
	LOCATION_INDOOR_LOCAL,
	LOCATION_INDOOR_INTERNAL,
	LOCATION_INDOOR_EXTERNAL,
	LOCATION_KEYPAD,
	LOCATION_BELL_BUTTON_MAIN,
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
		case LOCATION_BELL_BUTTON_GLASS:
			start=0;
			stop=1;
			break;
		case LOCATION_INDOOR_LOCAL:
			start=1;
			stop=2;
			break;
		case LOCATION_INDOOR_INTERNAL:
			start=2;
			stop=3;
			break;
		case LOCATION_INDOOR_EXTERNAL:
			start=3;
			stop=4;
			break;
		case LOCATION_KEYPAD:
			start=4;
			stop=9;
			break;
		case LOCATION_BELL_BUTTON_MAIN:
			start=9;
			stop=10;
			break;
		case LOCATION_STRIPE:
			start=10;
			stop=34;
			break;
		case LOCATION_ALL:
			start=0;
			stop=34;
			break;
	}

	for(i=start; i < stop; i++)
		led_set(udata, i, color);
	for(i=start; i < stop; i++)
		led_check(udata, i, color);
}

static uint32_t state2color(enum states2 curstate, enum states2 nextstate) {
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

	return color;
}

static void dump(struct userdata *udata) {
	printf("state dump: (local \"%s\" \"%s\") (internal \"%s\" \"%s\") (external \"%s\" \"%s\")\n",
		states[udata->curstate_local], states[udata->nextstate_local],
		states[udata->curstate_internal], states[udata->nextstate_internal],
		states[udata->curstate_external], states[udata->nextstate_external]);
}

static void display_state(struct userdata *udata) {
	pthread_mutex_lock(&mutex);

	dump(udata);

	/* local */
	uint32_t color_loc = state2color(udata->curstate_local, udata->nextstate_local);
	color_loc |= (MODE_FADE | 63);

	/* internal */
	uint32_t color_int = state2color(udata->curstate_internal, udata->nextstate_internal);
	if (color_int == RED && udata->bolt) /* closed + LOCKED -> dark red */
		color_int = RED2;
	color_int |= (MODE_FADE | 63);

	/* external */
	uint32_t color_ext = state2color(udata->curstate_external, udata->nextstate_external);
	color_ext |= (MODE_FADE | 63);

	uint32_t greenblink = GREEN | (MODE_BLINK | 8);

	gpio_write(&modegpio, 1);
	usleep(1000);
	sed_multi_led(udata, LOCATION_ALL, color_int);
	sed_multi_led(udata, LOCATION_INDOOR_LOCAL, color_loc);
	sed_multi_led(udata, LOCATION_INDOOR_EXTERNAL, color_ext);
	if (udata->buzzer_maindoor) {
		sed_multi_led(udata, LOCATION_KEYPAD, greenblink);
		sed_multi_led(udata, LOCATION_BELL_BUTTON_MAIN, greenblink);
	}
	if (udata->buzzer_glassdoor) {
		sed_multi_led(udata, LOCATION_BELL_BUTTON_GLASS, greenblink);
	}
	gpio_write(&modegpio, 0);

	pthread_mutex_unlock(&mutex);
}

static void set_state(struct userdata *udata, enum bus bus, int curstate, int nextstate) {
	printf("[bus=%d] curstate: %s - nextstate: %s\n", bus, states[curstate], states[nextstate]);

	switch(bus) {
		case LOCAL:
			if (curstate >= 0)
				udata->curstate_local = curstate;
			if (nextstate >= 0)
				udata->nextstate_local = nextstate;
			break;
		case INTERNAL:
			if (curstate >= 0)
				udata->curstate_internal = curstate;
			if (nextstate >= 0)
				udata->nextstate_internal = nextstate;
			break;
		case EXTERNAL:
			if (curstate >= 0)
				udata->curstate_external = curstate;
			if (nextstate >= 0)
				udata->nextstate_external = nextstate;
			break;
	}
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

static void on_connect(struct mosquitto *m, void *data, int res) {
	struct userdata *udata = (struct userdata*) data;
	const char *bus = mqtt_to_bus_str(m, data);
	enum bus b = mqtt_to_bus_id(m, data);
	int ret;

	fprintf(stderr, "Connected to %s bus!\n", bus);

	ret = mosquitto_subscribe(m, NULL, TOPIC_STATE_CUR, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC_STATE_CUR, ret);
		exit(1);
	}

	ret = mosquitto_subscribe(m, NULL, TOPIC_STATE_NEXT, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC_STATE_NEXT, ret);
		exit(1);
	}

	/* the following topics are only registered for the internal mqtt server */
	if (b != INTERNAL)
		return;

	ret = mosquitto_subscribe(m, NULL, TOPIC_BOLT_STATE, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC_BOLT_STATE, ret);
		exit(1);
	}

	ret = mosquitto_subscribe(m, NULL, TOPIC_MAIN_DOOR_BUZZER, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC_MAIN_DOOR_BUZZER, ret);
		exit(1);
	}

	ret = mosquitto_subscribe(m, NULL, TOPIC_GLASS_DOOR_BUZZER, 1);
	if (ret) {
		fprintf(stderr, "Error could not subscribe to %s: %d\n", TOPIC_GLASS_DOOR_BUZZER, ret);
		exit(1);
	}
}

static void on_disconnect(struct mosquitto *m, void *data, int res) {
	struct userdata *udata = (struct userdata*) data;
	const char *bus = mqtt_to_bus_str(m, data);
	enum bus b = mqtt_to_bus_id(m, data);

	fprintf(stderr, "Disconnected from %s MQTT server\n", bus);
	set_state(udata, b, STATE_DISCONNECTED, STATE_UNKNOWN);
	display_state(udata);

	/* register reconnect handler */
	alarm(30);
}

static void on_message(struct mosquitto *m, void *udata, const struct mosquitto_message *msg) {
	enum bus b = mqtt_to_bus_id(m, udata);

	if(!strcmp(TOPIC_STATE_CUR, msg->topic)) {
		int curstate = str2state(msg->payload, msg->payloadlen);
		set_state(udata, b, curstate, -1);
		display_state(udata);
		return;
	} else if(!strcmp(TOPIC_STATE_NEXT, msg->topic)) {
		int nextstate = str2state(msg->payload, msg->payloadlen);
		set_state(udata, b, -1, nextstate);
		display_state(udata);
		return;
	} else if (!strcmp(TOPIC_BOLT_STATE, msg->topic) && msg->payloadlen) {
		((struct userdata *) udata)->bolt = ((char*) msg->payload)[0] == '1';
		display_state(udata);
		return;
	} else if (!strcmp(TOPIC_MAIN_DOOR_BUZZER, msg->topic) && msg->payloadlen) {
		((struct userdata *) udata)->buzzer_maindoor = ((char*) msg->payload)[0] == '1';
		display_state(udata);
		return;
	} else if (!strcmp(TOPIC_GLASS_DOOR_BUZZER, msg->topic) && msg->payloadlen) {
		((struct userdata *) udata)->buzzer_glassdoor = ((char*) msg->payload)[0] == '1';
		display_state(udata);
		return;
	}

	fprintf(stderr, "Ignored message with wrong topic\n");
	return;
}

void on_alarm(int signal) {
	struct userdata *udata = globaldata;

	if (udata->curstate_internal == STATE_DISCONNECTED) {
		mosquitto_reconnect_async(udata->mqtt_internal);
		alarm(30);
	} else if (udata->curstate_external == STATE_DISCONNECTED) {
		mosquitto_reconnect_async(udata->mqtt_external);
		alarm(30);
	}
}

int mqtt_init(enum bus b, struct userdata *udata) {
	struct mosquitto *mosq;
	char *user, *pass, *cert, *host;
	int port, keepalv, ret;
	FILE *cfg;

	cfg = cfg_open();
	user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	keepalv = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);
	if (b == INTERNAL) {
		cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
		host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
		port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	} else if (b == EXTERNAL) {
		cert = cfg_get_default(cfg, "mqtt-broker-external-cert", MQTT_BROKER_EXTERNAL_CERT);
		host = cfg_get_default(cfg, "mqtt-broker-external-host", MQTT_BROKER_EXTERNAL_HOST);
		port = cfg_get_int_default(cfg, "mqtt-broker-external-port", MQTT_BROKER_EXTERNAL_PORT);
	} else {
		return -1;
		cfg_close(cfg);
	}
	cfg_close(cfg);

	/* create mosquitto client instance */
	mosq = mosquitto_new("space-status-leds", true, udata);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	/* store pointer */
	if (b == INTERNAL)
		udata->mqtt_internal = mosq;
	else if(b == EXTERNAL)
		udata->mqtt_external = mosq;

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

	/* setup tls parameters */
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

	/* mainloop */
	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	free(user);
	free(pass);
	free(cert);
	free(host);

	return 0;
}

static void handle_inotify(int fd) {
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	int i;
	ssize_t len;
	char *ptr;

	for (;;) {
		/* Read some events. */
		len = read(fd, buf, sizeof buf);
		if (len == -1 && errno != EAGAIN) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		/* If the nonblocking read() found no events to read, then
		it returns -1 with errno set to EAGAIN. In that case,
		we exit the loop. */
		if (len <= 0)
			break;

		/* Loop over all events in the buffer */
		printf("modified files: ");
		for (ptr = buf; ptr < buf + len;
			ptr += sizeof(struct inotify_event) + event->len) {

			event = (const struct inotify_event *) ptr;

			/* Print the name of the file */
			if (event->len)
				printf("%s ", event->name);
		}

		printf("\n");
	}
}

static char* file_read_line(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "could not open %s\n", path);
		return NULL;
	}

	char *line = NULL;
	size_t len = 0, read;

	read = getline(&line, &len, f);
	if (read < 0) {
		fprintf(stderr, "could not read %s\n", path);
		return NULL;
	}

	/* remove newline */
	line[strlen(line)-1] = '\0';

	fclose(f);

	return line;
}

int main(int argc, char **argv) {
	struct userdata *udata;
	int ret = 0;

	mosquitto_lib_init();
	signal(SIGALRM, on_alarm);

	FILE *cfg = cfg_open();
	char *statedir = cfg_get_default(cfg, "statedir", STATEDIR);
	int i2c_busid = cfg_get_int_default(cfg, "i2c-leds-bus", I2C_LEDS_BUS);
	int i2c_devid = cfg_get_int_default(cfg, "i2c-leds-dev", I2C_LEDS_DEV);
	cfg_close(cfg);

	char *status_file, *status_next_file;

	udata = malloc(sizeof(*udata));
	if(!udata) {
		printf("out of memory!\n");
		return 1;
	}
	globaldata = udata;

	/* initial state unknown */
	set_state(udata, LOCAL, STATE_UNKNOWN, STATE_UNKNOWN);
	set_state(udata, INTERNAL, STATE_DISCONNECTED, STATE_UNKNOWN);
	set_state(udata, EXTERNAL, STATE_DISCONNECTED, STATE_UNKNOWN);
	display_state(udata);

	ret = mqtt_init(INTERNAL, udata);
	if (ret) {
		printf("Failed to init internal MQTT connection!\n");
		return 1;
	}

	ret = mqtt_init(EXTERNAL, udata);
	if (ret) {
		printf("Failed to init external MQTT connection!\n");
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

	ret = asprintf(&status_file, "%s/status", statedir);
	if (ret <= 0)
		return ret;

	ret = asprintf(&status_next_file, "%s/status-next", statedir);
	if (ret <= 0)
		return ret;

	ifd = inotify_init1(IN_NONBLOCK);
	if (ifd == -1)
		return 1;

	printf("Watched state-directory: %s\n", statedir);
	wfd = inotify_add_watch(ifd, statedir, IN_MODIFY | IN_DELETE);
	if (wfd == -1)
		return 1;

	struct pollfd fdset[1];
	fdset[0].fd = ifd;
	fdset[0].events = POLLIN;

	for(;;) {
		char *status_str = file_read_line(status_file);
		char *next_status_str = file_read_line(status_next_file);

		enum states2 status = STATE_UNKNOWN;
		if (status_str) {
			status = str2state(status_str, strlen(status_str));
			free(status_str);
		}

		enum states2 next_status = STATE_UNKNOWN;
		if (next_status_str) {
			next_status = str2state(next_status_str, strlen(next_status_str));
			free(next_status_str);
		}

		fprintf(stderr, "state file: %d %d\n", status, next_status);

		set_state(udata, LOCAL, status, next_status);
		display_state(udata);

		ret = poll(fdset, 1, POLL_TIMEOUT);
		if (ret < 0) {
				fprintf(stderr, "Failed to poll local state file: %d\n", ret);
				continue;
		}

		/* wait until all files have been written to reduce MQTT spam  */
		sleep(1);

		if (fdset[0].revents & POLLIN)
			handle_inotify(ifd);
	}

	free(statedir);
	free(udata);

	return 0;
}
