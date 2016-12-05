#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <linux/gpio.h>
#include "../common/i2c.h"
#include "../keyboard/gpio.h"
#include "../common/config.h"
#include "interface.h"

int dev;
int irq;
int button_setup;
int button_unlock;
int button_lock;
bool alarm_in_progress = false;

struct gpiodesc gpios[] = {
	{ "platform/3f200000.gpio",  8, "cfa1000 unlock", GPIO_OUTPUT, GPIO_ACTIVE_LOW, -1, -1 },
	{ "platform/3f200000.gpio",  9, "cfa1000 irq",    GPIO_INPUT,  GPIO_ACTIVE_LOW, -1, -1 },
	{ "platform/3f200000.gpio", 10, "cfa1000 setup",  GPIO_OUTPUT, GPIO_ACTIVE_LOW, -1, -1 },
	{ "platform/3f200000.gpio", 11, "cfa1000 lock",   GPIO_OUTPUT, GPIO_ACTIVE_LOW, -1, -1 },
	{ 0 }
};

#define GPIO_UNLOCK 0
#define GPIO_IRQ 1
#define GPIO_SETUP 2
#define GPIO_LOCK 3

#define TOPIC "/access-control-system/main-door/bolt-state"

#define GPIO_TIMEOUT 5 * 60 * 1000

#define MCP23017_INTCON_A 0x08
#define MCP23017_INTCON_B 0x09
#define MCP23017_GPINTEN_A 0x04
#define MCP23017_GPINTEN_B 0x05
#define MCP23017_IOCON_A 0x0a
#define MCP23017_IOCON_B 0x0b

void on_alarm(int signal) {
	printf("button unpressed!\n");
	gpio_write(&gpios[GPIO_SETUP], false);
	gpio_write(&gpios[GPIO_LOCK], false);
	gpio_write(&gpios[GPIO_UNLOCK], false);
	alarm_in_progress = false;
}

static void non_blocking_stdin() {
	struct termios ttystate, ttysave;

	/* make STDIN non-blocking */
	int flags = fcntl(0, F_GETFL, 0);
	fcntl(0, F_SETFL, flags | O_NONBLOCK);

	//get the terminal state
	tcgetattr(STDIN_FILENO, &ttystate);
	ttysave = ttystate;
	//turn off canonical mode and echo
	ttystate.c_lflag &= ~(ICANON | ECHO);
	//minimum of number input read.
	ttystate.c_cc[VMIN] = 1;
	tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
}

int main(int argc, char **argv) {
	int ret, i;

	FILE *cfg = cfg_open();
	int i2c_busid = cfg_get_int_default(cfg, "abus-cfa1000-i2c-bus", -1);
	int i2c_devid = cfg_get_int_default(cfg, "abus-cfa1000-i2c-dev", -1);
	cfg_close(cfg);

	if (i2c_busid < 0 || i2c_devid < 0) {
		fprintf(stderr, "Please configure busid and devid!\n");
		return 1;
	}

	dev = i2c_open(i2c_busid, i2c_devid);
	if (dev < 0) {
		fprintf(stderr, "Could not open I2C device: %d\n", dev);
		return 1;
	}

	for (i = 0; gpios[i].dev; i++) {
		int err = gpio_init(&gpios[i]);
		if (err) {
			fprintf(stderr, "could not init gpio \"%s\": %d!\n", gpios[i].name, err);
			return 1;
		}
	}

	/* irq: compare against previous value */
	i2c_write16(dev, MCP23017_INTCON_A, 0x0000);

	/* irq: check all bits */
	i2c_write16(dev, MCP23017_GPINTEN_A, 0xFFFF);

	/* irq: enable mirror mode (IRQs are OR'd) */
	i2c_write16(dev, MCP23017_IOCON_A, 0x7070);

	struct pollfd fdset[2];
	fdset[0].fd = gpios[GPIO_IRQ].evfd;
	fdset[0].events = POLLIN;
	fdset[1].fd = 0; /* STDIN */
	fdset[1].events = POLLIN | POLLPRI;

	non_blocking_stdin();

	/* alarm handler for disabling gpios */
	signal(SIGALRM, on_alarm);

	setbuf(stdout, NULL); // TODO: remove?

	printf("You can press buttons: s=setup, S=setup (long), l=lock, u=unlock\n");

	struct display_data_t olddisp = { .symbol = '\0', .state = LOCK_STATE_UNKNOWN };
	char irqstate = -1;
	for (;;) {
		struct display_data_t disp = display_read(dev);
		char *state = lock_state_str(disp.state);
		char kbddata;

		if (olddisp.symbol != disp.symbol || olddisp.state != disp.state) {
			printf("\rstate=%s (disp=%c) [irq=%hhd]", state, disp.symbol, irqstate);
			fflush(stdout);
		}

		olddisp = disp;

		ret = poll(fdset, 2, GPIO_TIMEOUT);
		if(ret < 0 && errno != EINTR && errno != EAGAIN) {
				printf("\n"); /* terminate state rollback line */
				fprintf(stderr, "Failed to poll gpio: %d (errno=%d)\n", ret, errno);
				return 1;
		}

		/* gpio change */
		if ((fdset[0].revents & POLLIN) != 0) {
			struct gpioevent_data event;

			ret = read(gpios[GPIO_IRQ].evfd, &event, sizeof(event));
			if (ret < 0) {
				fprintf(stderr, "read failed: %d\n", errno);
				return 1;
			}

			if (event.id == GPIOEVENT_EVENT_RISING_EDGE)
				irqstate = 1;
			else
				irqstate = 0;
		}

		if (alarm_in_progress)
			continue;

		ret = read(0, &kbddata, 1);
		if(ret !=1)
			continue;

		switch(kbddata) {
			case 'S':
				printf("\nlong setup pressed\n");
				gpio_write(&gpios[GPIO_SETUP], true);
				alarm_in_progress = true;
				alarm(3);
				break;
			case 's':
				printf("\nsetup pressed\n");
				gpio_write(&gpios[GPIO_SETUP], true);
				alarm_in_progress = true;
				alarm(1);
				break;
			case 'l':
				printf("\nlocked pressed\n");
				gpio_write(&gpios[GPIO_LOCK], true);
				alarm_in_progress = true;
				alarm(1);
				break;
			case 'u':
				printf("\nunlocked pressed\n");
				gpio_write(&gpios[GPIO_UNLOCK], true);
				alarm_in_progress = true;
				alarm(1);
				break;
			default:
				break;
		}
	}

	i2c_close(dev);
	close(irq);
	close(button_setup);
	close(button_unlock);
	close(button_lock);

	return 0;
}
