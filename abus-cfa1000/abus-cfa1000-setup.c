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

int dev;
int irq;
int button_setup;
int button_unlock;
int button_lock;
bool alarm_in_progress = false;

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
	gpio_write(button_setup, false);
	gpio_write(button_unlock, false);
	gpio_write(button_lock, false);
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
	int ret;

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

	irq = gpio_get("abus-cfa1000-irq");
	if (irq < 0) {
		fprintf(stderr, "Could not open IRQ GPIO: %d\n", irq);
		return 1;
	}

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

	/* irq: compare against previous value */
	i2c_write16(dev, MCP23017_INTCON_A, 0x0000);

	/* irq: check all bits */
	i2c_write16(dev, MCP23017_GPINTEN_A, 0xFFFF);

	/* irq: enable mirror mode (IRQs are OR'd) */
	i2c_write16(dev, MCP23017_IOCON_A, 0x7070);

	struct pollfd fdset[2];
	fdset[0].fd = irq;
	fdset[0].events = POLLPRI;
	fdset[1].fd = 0; /* STDIN */
	fdset[1].events = POLLIN | POLLPRI;

	non_blocking_stdin();

	/* alarm handler for disabling gpios */
	signal(SIGALRM, on_alarm);

	//setbuf(stdout, NULL);

	printf("You can press buttons: s=setup, l=lock, u=unlock\n");

	struct display_data_t olddisp = { .symbol = '\0', .state = LOCK_STATE_UNKNOWN };
	for (;;) {
		char irqstate = gpio_read(irq);
		struct display_data_t disp = display_read(dev);
		char *state = lock_state_str(disp.state);
		char kbddata;

		if (olddisp.symbol != disp.symbol || olddisp.state != disp.state) {
			printf("\rstate=%s (disp=%c) [irq=%d]", state, disp.symbol, irqstate);
			fflush(stdout);
		}

		olddisp = disp;

		ret = poll(fdset, 2, GPIO_TIMEOUT);
		if(ret < 0 && errno != EINTR && errno != EAGAIN) {
				printf("\n"); /* terminate state rollback line */
				fprintf(stderr, "Failed to poll gpio: %d (errno=%d)\n", ret, errno);
				return 1;
		}

		if (alarm_in_progress)
			continue;

		ret = read(0, &kbddata, 1);
		if(ret !=1)
			continue;

		switch(kbddata) {
			case 's':
				printf("\nsetup pressed\n");
				gpio_write(button_setup, true);
				alarm_in_progress = true;
				alarm(1);
				break;
			case 'l':
				printf("\nlocked pressed\n");
				gpio_write(button_lock, true);
				alarm_in_progress = true;
				alarm(1);
				break;
			case 'u':
				printf("\nunlocked pressed\n");
				gpio_write(button_unlock, true);
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
