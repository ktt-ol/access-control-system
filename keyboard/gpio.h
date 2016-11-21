#ifndef __GPIO_H
#define __GPIO_H

#include <stdint.h>
#include <stdbool.h>

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

int gpio_init(struct gpiodesc *gpio);
int gpio_write(struct gpiodesc *gpio, bool value);

#endif
