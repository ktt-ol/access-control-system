#ifndef __I2C_H
#define __I2C_H

#include <stdint.h>

int i2c_open(int bus, int dev);
int i2c_close(int file);
int i2c_write(int file, uint8_t reg, uint8_t val);

#endif
