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

#include "gpio.h"

#define KEY_RELEASE 0
#define KEY_PRESS 1
#define KEY_KEEPING_PRESSED 2

#define BUFFER_SIZE 32

struct gpiodesc main_door_buzzer = {"platform/3f200000.gpio", 24, "maindoor buzzer", true, true, -1, -1};

void input(char *code) {
	if (strcmp(code, "00000000")) {
		printf("incorrect code: %s\n", code);
		return;
	}

	printf("correct code, open main door!\n");
	gpio_write(&main_door_buzzer, true);
	alarm(3);

	return;
}

void on_alarm(int signal) {
	printf("timeout, close main door!\n");
	gpio_write(&main_door_buzzer, false);
}

int main (int argc, char *argv[]) {
	int i, fd, pos, err;
	struct input_event ev[64];
	char buffer[BUFFER_SIZE + 1] = {0};

	if ((fd = open("/dev/input/by-path/platform-gpio-keys-event", O_RDONLY)) < 0) {
		perror("Couldn't open input device");
		return 1;
	}

	err = gpio_init(&main_door_buzzer);
	if (err) {
		fprintf(stderr, "Couldn't open maindoor buzzer gpio: %d\n", err);
		return 1;
	}

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
