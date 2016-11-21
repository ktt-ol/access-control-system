/*
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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "gpio.h"
#define CONFIG_SYSFS_GPIO
#include "config.h"

#define MAX_BUF 64

static int gpio_open(int gpio, bool output, bool active_low) {
	int fd;
	char buf[MAX_BUF];

	/* export GPIO */
	fd = open("/sys/class/gpio/export", O_WRONLY);
	if(fd == -1) return -1;
	snprintf(buf, MAX_BUF, "%d", gpio);
	write(fd, buf, strlen(buf));
	close(fd);

	/* set direction */
	snprintf(buf, MAX_BUF, "/sys/class/gpio/gpio%d/direction", gpio);
	fd = open(buf, O_WRONLY);
	if(fd == -1) return -1;
	if (output)
		write(fd, "out", 3);
	else
		write(fd, "in", 2);
	close(fd);

	/* set active_low */
	snprintf(buf, MAX_BUF, "/sys/class/gpio/gpio%d/active_low", gpio);
	fd = open(buf, O_WRONLY);
	if(fd == -1) return -1;
	write(fd, active_low ? "1" : "0", 1);
	close(fd);

	if (!output) {
		snprintf(buf, MAX_BUF, "/sys/class/gpio/gpio%d/edge", gpio);
		fd = open(buf, O_WRONLY);
		if(fd == -1) return -1;
		write(fd, "both", 4);
		close(fd);
	}

	/* open gpio */
	snprintf(buf, MAX_BUF, "/sys/class/gpio/gpio%d/value", gpio);
	if (output)
		fd = open(buf, O_WRONLY);
	else
		fd = open(buf, O_RDONLY);

	return fd;
}

int gpio_get(char *name) {
	int i;

	for(i=0; gpios[i].name; i++) {
		if(strcmp(gpios[i].name, name))
			continue;
		return gpio_open(gpios[i].gpio, gpios[i].direction, gpios[i].active_low);
	}
}

volatile bool gpio_read(int fd) {
	char value;
	lseek(fd, 0, SEEK_SET);
	read(fd, &value, 1);
	return (value == '1');
}

void gpio_write(int fd, bool enable) {
	char value = enable ? '1' : '0';
	lseek(fd, 0, SEEK_SET);
	write(fd, &value, 1);
}
