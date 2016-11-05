#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

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

static bool led_get(int fd, uint8_t i, uint32_t *val) {
	uint32_t readval;
	int retries, err;

	for(retries = 0; retries < 3; retries++) {
		err = i2c_smbus_read_i2c_block_data(fd, i, 4, (uint8_t*) &readval);
		if (err == -1)
			continue;
		*val = ntohl(readval);
		return true;
	}

	return false;

}

static void led_print(int fd, uint8_t i) {
	uint32_t val;

	if (!led_get(fd, i, &val)) {
		printf("failed!\n");
		return;
	}

	printf("val: %08x\n", val);
}

static bool led_set(int fd, uint8_t i, uint32_t val) {
	uint32_t beval = htonl(val);
	uint32_t readval;
	int retries, err;

	for(retries = 0; retries < 5; retries++) {
		err = i2c_smbus_write_i2c_block_data(fd, i, 4, (uint8_t*) &beval);
		if (err == -1)
			continue;
		usleep(1000);
		err = i2c_smbus_read_i2c_block_data(fd, i, 4, (uint8_t*) &readval);
		if (err == -1)
			continue;
		readval = ntohl(readval);
		if ((val & 0xffffffc0) == (readval & 0xffffffc0))
			return true;
	}

	printf("failed %d!\n", i);
	return false;
}

static void led_all(int fd, uint32_t val) {
	int i;
	led_set(fd, 0xff, 0xffffffff);
	for(i=0; i<50; i++)
		led_set(fd, i, val);
	led_set(fd, 0xff, 0x00000000);

}

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "%s <i2c-dev> <led> <color>\n", argv[0]);
		fprintf(stderr, "\ti2c-dev: 1 for /dev/i2c-1\n");
		fprintf(stderr, "\tled:     id or \"all\"\n");
		fprintf(stderr, "\tcolor:   0xRRGGBBcc\n");
		return 1;
	}

	int i2c = atoi(argv[1]);
	uint8_t led = strtoul(argv[2], NULL, 0) & 0xff;
	uint32_t val = strtoul(argv[3], NULL, 0);

	int fd = i2c_open(i2c, 0x23);
	if(fd < 0) {
		fprintf(stderr, "Failed to open i2c device!\n");
		return fd;
	}

	if (!strcmp(argv[2], "all"))
		led_all(fd, val);
	else
		led_set(fd, led, val);

	i2c_close(fd);
}
