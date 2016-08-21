#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include "../common/config.h"
#include "../common/i2c.h"
#include "../common/gpio.h"
#include "interface.h"

int button_setup;
int button_unlock;
int button_lock;

#define TOPIC "/access-control-system/main-door/bolt-state"

static void button_press(int button, int time) {
	gpio_write(button, true);
	sleep(time);
	gpio_write(button, false);

}

int main(int argc, char **argv) {
	int ret;

	button_setup = gpio_get("abus-cfa1000-button-setup");
	if (button_setup < 0) {
		fprintf(stderr, "Could not open button-setup GPIO: %d\n", button_setup);
		return 1;
	}

	button_unlock = gpio_get("abus-cfa1000-button-unlock");
	if (button_unlock < 0) {
		fprintf(stderr, "Could not open button-unlock GPIO: %d\n", button_unlock);
		return 1;
	}

	button_lock = gpio_get("abus-cfa1000-button-lock");
	if (button_lock < 0) {
		fprintf(stderr, "Could not open button-lock GPIO: %d\n", button_lock);
		return 1;
	}

	printf("ABUS CFA1000 autocalibration for Mainframe door!\n");

	gpio_write(button_setup, false);
	gpio_write(button_unlock, false);
	gpio_write(button_lock, false);

	sleep(5);

	button_press(button_setup, 3);
	sleep(3);
	button_press(button_setup, 1);
	sleep(20);
	button_press(button_lock, 1);
	sleep(5);
	button_press(button_lock, 1);
	sleep(5);
	button_press(button_setup, 1);
	sleep(20);
	button_press(button_unlock, 1);
	sleep(5);
	button_press(button_unlock, 1);
	sleep(5);
	button_press(button_setup, 1);
	sleep(3);
	button_press(button_setup, 1);
	sleep(3);

	close(button_setup);
	close(button_unlock);
	close(button_lock);

	return 0;
}
