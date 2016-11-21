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
#include "../common/config.h"

#define GPIO_TIMEOUT 1000 * 60 * 10

static inline int check_prefix(const char *str, const char *prefix) {
        return strlen(str) > strlen(prefix) &&
                strncmp(str, prefix, strlen(prefix)) == 0;
}

char *symlink_fname(const char *link) {
	char buf[512];
	char *tmp;
	ssize_t s;

	if(!link)
		return NULL;

	s = readlink(link, buf, sizeof(buf)-1);
	if (!s)
		return NULL;
	buf[s] = '\0';
	
	tmp = strrchr(buf, '/');
	if (!tmp)
		return NULL;

	return strdup(tmp+1);
}

char *chip2dev(const char *chipdev) {
	char *path, *result, *subsystem, *subsystempath;
	char buf[512];
	ssize_t s;
	int i;
	int t;

	asprintf(&path, "/sys/bus/gpio/devices/%s", chipdev);
	s = readlink(path, buf, sizeof(buf)-1);
	free(path);
	if (!s)
		return NULL;
	buf[s] = '\0';

	for(t = 0, i = s; i > 0; i--) {
		if (buf[i] == '/') {
			if (t == 0) {
				buf[i] = '\0';
				t++;
				continue;
			}

			asprintf(&subsystempath, "/sys/bus/gpio/devices/%s/subsystem", buf);
			subsystem = symlink_fname(subsystempath);
			free(subsystempath);

			asprintf(&result, "%s/%s", subsystem, buf+i+1);
			free(subsystem);

			return result;
		}
	}

	return NULL;
}

char* find_gpio_dev(char *dev) {
	const struct dirent *ent;
	char *desc;
	DIR *dp = opendir("/dev");
	if (!dp)
		return NULL;

	while (ent = readdir(dp), ent) {
		if (check_prefix(ent->d_name, "gpiochip")) {
			desc = chip2dev(ent->d_name);
			if (strcmp(dev, desc) == 0) {
				free(desc);
				asprintf(&desc, "/dev/%s", ent->d_name);
				return desc;
			}
			free(desc);
		}
	}

	return NULL;
}

struct gpiodesc {
	char *dev;
	uint8_t offset;
	char *name;
	bool output;
	bool active_low;

	/* private */
	int fd;
	int evfd;
};

struct mqttgpio {
	char *topic;
	struct gpiodesc desc;

	/* private */
	uint8_t cached;
};

struct mqttgpio gpios[] = {
	{"/access-control-system/main-door/bell-button", { "i2c/1-0021", 3, "maindoor bell button", false, false, -1, -1 }, -1},
	{"/access-control-system/glass-door/bell-button", { "i2c/1-0022", 7, "glassdoor bell button", false, true, -1, -1 }, -1},
	{"/access-control-system/glass-door/bolt-contact", { "platform/3f200000.gpio", 4, "glassdoor bolt sw", false, true, -1, -1 }, -1},
	{"/access-control-system/main-door/reed-switch", { "platform/3f200000.gpio", 25, "maindoor reed sw", false, false, -1, -1 }, -1},
	{}
};

int gpio_init(struct gpiodesc *gpio) {
	char *dev = find_gpio_dev(gpio->dev);
	if (!dev)
		return -ENODEV;

	gpio->fd = open(dev, 0);
	free(dev);

	if (gpio->fd < 0)
		return -errno;

	struct gpioevent_request req;
	struct gpioevent_data event;

	req.lineoffset = gpio->offset;
	strncpy(req.consumer_label, gpio->name, 31);

	req.handleflags = (gpio->output) ? GPIOHANDLE_REQUEST_OUTPUT : GPIOHANDLE_REQUEST_INPUT; 
	if (gpio->active_low)
		req.handleflags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;

	if (!gpio->output) {
		req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;

		int err = ioctl(gpio->fd, GPIO_GET_LINEEVENT_IOCTL, &req);
		if (err < 0) {
			close(gpio->fd);
			return -errno;
		}

		gpio->evfd = req.fd;
	}

	return 0;
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
	mosq = mosquitto_new("acs-gpio-sensor", true, NULL);
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

int main(int argc, char **argv) {
	struct gpioevent_data event;
	struct mosquitto *mosq;
	struct pollfd *fdset;
	int i, nfds;
	int err;

	/* setup gpios */
	for (i = 0; gpios[i].desc.dev; i++) {
		int err = gpio_init(&gpios[i].desc);
		if (err) {
			fprintf(stderr, "could not init gpio \"%s\": %d!\n", gpios[i].desc.name, err);
			return 1;
		}
	}
	nfds = i;

	/* setup fdset */
	fdset = calloc(sizeof(struct pollfd), nfds);
	for (i = 0; gpios[i].desc.dev; i++) {
		fdset[i].fd = gpios[i].desc.evfd;
		fdset[i].events = POLLIN|POLLPRI;
	}
	
	mosq = mqtt_init();
	if (!mosq)
		return 1;

	for(;;) {
		err = poll(fdset, nfds, GPIO_TIMEOUT);
		if (err < 0) {
			fprintf(stderr, "failed to poll gpios: %d\n", err);
			return 1;
		}

		for (i=0; i < nfds; i++) {
			bool state;

			if ((fdset[i].revents & POLLIN) == 0)
				continue;

			err = read(gpios[i].desc.evfd, &event, sizeof(event));
			if (err < 0) {
				fprintf(stderr, "read failed: %d\n", errno);
				return 1;
			}

			state = (event.id == GPIOEVENT_EVENT_RISING_EDGE);

			if (state == gpios[i].cached)
				continue;
			gpios[i].cached = state;

			printf("gpio %s: %d\n", gpios[i].topic, state);

			/* publish state */
			err = mosquitto_publish(mosq, NULL, gpios[i].topic, 1, state ? "1" : "0", 0, true);
			if (err) {
				fprintf(stderr, "Error could not send message: %d\n", err);
				return 1;
			}
		}
	}

	return 0;
};
