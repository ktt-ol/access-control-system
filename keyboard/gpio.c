/*
* Access Control System - GPIO helpers
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

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <linux/gpio.h>

#include "gpio.h"

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

int gpio_init(struct gpiodesc *gpio) {
	char *dev = find_gpio_dev(gpio->dev);
	if (!dev)
		return -ENODEV;

	gpio->fd = open(dev, 0);
	free(dev);

	if (gpio->fd < 0)
		return -errno;


	if (!gpio->output) {
		struct gpioevent_request req;

		req.lineoffset = gpio->offset;
		strncpy(req.consumer_label, gpio->name, 31);

		req.handleflags = GPIOHANDLE_REQUEST_INPUT;
		if (gpio->active_low)
			req.handleflags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;

		req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;

		int err = ioctl(gpio->fd, GPIO_GET_LINEEVENT_IOCTL, &req);
		if (err < 0) {
			close(gpio->fd);
			return -errno;
		}

		gpio->evfd = req.fd;
	} else {
		struct gpiohandle_request req;

		req.lines = 1;
		req.lineoffsets[0] = gpio->offset;
		strncpy(req.consumer_label, gpio->name, 31);

		req.flags = GPIOHANDLE_REQUEST_OUTPUT;
		if (gpio->active_low)
			req.flags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;

		int err = ioctl(gpio->fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
		if (err < 0) {
			close(gpio->fd);
			return -errno;
		}

		gpio->evfd = req.fd;
	}

	return 0;
}

int gpio_write(struct gpiodesc *gpio, bool value) {
	struct gpiohandle_data data;
	data.values[0] = value;
	return ioctl(gpio->evfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
}
