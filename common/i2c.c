#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/inet.h>

#include "i2c.h"

int i2c_open(int bus, int dev) {
	int file;
	char filename[20];
  
	sprintf(filename,"/dev/i2c-%d", bus);
	if ((file = open(filename,O_RDWR)) < 0)
		return -errno;
	
	if (ioctl(file, I2C_SLAVE, dev) < 0)
		return -errno;
	
	return file;
}

int i2c_close(int file) {
	return close(file);
}

int i2c_write(int file, uint8_t reg, uint8_t val) {
	return i2c_smbus_write_byte_data(file, reg, val);
}

int i2c_read(int file, uint8_t reg) {
	return i2c_smbus_read_byte_data(file, reg);
}

int i2c_write16(int file, uint8_t reg, uint16_t val) {
	return i2c_smbus_write_word_data(file, reg, val);
}

int i2c_read16(int file, uint8_t reg) {
	return i2c_smbus_read_word_data(file, reg);
}

int i2c_write32(int file, uint8_t reg, uint32_t data) {
	data = htonl(data);
	return i2c_smbus_write_i2c_block_data(file, reg, 4, (uint8_t*) &data);
}

uint32_t i2c_read32(int file, uint8_t reg) {
	uint32_t data;
	i2c_smbus_read_i2c_block_data(file, reg, 4, (uint8_t*) &data);
	data = ntohl(data);
	return data;
}
