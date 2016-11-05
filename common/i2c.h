#ifndef __I2C_H
#define __I2C_H

#include <stdint.h>

int i2c_open(int bus, int dev);
int i2c_close(int file);
int i2c_read(int file, uint8_t reg);
int i2c_write(int file, uint8_t reg, uint8_t val);
int i2c_read16(int file, uint8_t reg);
int i2c_write16(int file, uint8_t reg, uint16_t val);
int i2c_write32(int file, uint8_t reg, uint32_t data);
uint32_t i2c_read32(int file, uint8_t reg);

#endif
