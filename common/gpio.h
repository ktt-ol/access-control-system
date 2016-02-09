#ifndef __GPIO_H
#define __GPIO_H

#include <stdbool.h>

int gpio_open(int id, bool output);
volatile bool gpio_read(int fd);
void gpio_write(int fd, bool enable);

#endif
