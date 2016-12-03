#ifndef __I2C_H
#define __I2C_H

#include <avr/io.h>
#include <stdint.h>
#include <stdbool.h>

struct i2c_data {
	uint8_t data0;
	uint8_t data1;
	uint8_t data2;
	uint8_t data3;
};

void i2c_init(uint8_t own_address);
void i2c_enable();
void i2c_disable();
void i2c_recv(uint8_t reg, struct i2c_data  val);
void i2c_send(uint8_t reg, struct i2c_data *val);

static inline bool i2c_active() {
	return (USICR & USIOIE);
}

#endif
