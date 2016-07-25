#ifndef __GPIO_H
#define __GPIO_H

#include <stdbool.h>

#define ACTIVE_LOW 1
#define ACTIVE_HIGH 0

enum gpio_direction {
	GPIO_DIRECTION_INPUT = 0,
	GPIO_DIRECTION_OUTPUT = 1,
};

int gpio_get(char *name);
volatile bool gpio_read(int fd);
void gpio_write(int fd, bool enable);

#endif
