#ifndef __GPIO_H
#define __GPIO_H

#include <stdint.h>
#include <stdbool.h>

enum gpio_direction {
	GPIO_INPUT = 0,
	GPIO_OUTPUT = 1
};

enum gpio_flags {
	GPIO_ACTIVE_HIGH = 0,
	GPIO_ACTIVE_LOW = 1,
	GPIO_DEFAULT_SET = 2,
};

struct gpiodesc {
	char *dev;
	uint8_t offset;
	char *name;
	enum gpio_direction direction;
	enum gpio_flags flags;

	/* private */
	int fd;
	int evfd;
};

int gpio_init(struct gpiodesc *gpio);
int gpio_write(struct gpiodesc *gpio, bool value);

#endif
