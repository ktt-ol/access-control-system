#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#define display_size 32+1
#define ETH_DEV "eth0"
#define SERIAL_DEV "/dev/ttyUSB0"

const static char clear_display_cmd[] = {0xfe, 0x01};
#define clear_display(fd) write(fd, clear_display_cmd, sizeof(clear_display_cmd));

static int serial_setup(int fd, int speed) {
	struct termios tty;
	memset(&tty, 0, sizeof(tty));
	if(tcgetattr(fd, &tty) != 0) {
		fprintf(stderr, "error %d from tcgetattr", errno);
		return -1;
	}

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	/* 8N1, non blocking, no modem controls */
	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
	tty.c_iflag &= ~IGNBRK;
	tty.c_lflag = 0;
	tty.c_oflag = 0;
	tty.c_cc[VMIN]  = 0;
	tty.c_cc[VTIME] = 5;
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	tty.c_cflag |= (CLOCAL | CREAD);
	tty.c_cflag &= ~(PARENB | PARODD);
	tty.c_cflag |= 0;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if(tcsetattr(fd, TCSANOW, &tty) != 0) {
		fprintf(stderr, "error %d from tcsetattr", errno);
		return -1;
	}
	return 0;
}

static char *get_ip_addr(char *dev) {
	int fd;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);
	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);

	return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

static int get_carrier(char *dev) {
	int fd;
	struct ifreq ifr;
	struct ethtool_value edata;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	edata.cmd = ETHTOOL_GLINK;
	ifr.ifr_data = (char *) &edata;
	strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);
	ioctl(fd, SIOCETHTOOL, &ifr);
	close(fd);

	return edata.data;
}

static char *get_time() {
	char *result = malloc(20);
	if(!result)
		return NULL;
	time_t rawtime = time(NULL);
	struct tm *now = localtime(&rawtime);
	strftime(result, 20, "%Y-%m-%d %H:%M", now);
	return result;
}

int main(int argc, char **argv) {
	char *portname = SERIAL_DEV;
	char *msg = malloc(display_size);
	char *ip, *time;

	int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
	if(fd < 0) {
		fprintf(stderr, "Could not open serial device!\n");
		return 1;
	}

	serial_setup(fd, B9600);
	clear_display(fd);

	fprintf(stderr, "Display initialized!\n");

	while(1) {
		snprintf(msg, display_size, "Spaceschalter   2.0");
		write(fd, msg, strlen(msg));

		sleep(3);
		clear_display(fd);

		if(get_carrier(ETH_DEV)) {
			ip = get_ip_addr(ETH_DEV);
			snprintf(msg, display_size, "IP:             %s", ip);
			write(fd, msg, strlen(msg));
		} else {
			snprintf(msg, display_size, "No link detected");
			write(fd, msg, strlen(msg));
		}

		sleep(3);
		clear_display(fd);

		time = get_time();
		snprintf(msg, display_size, "Time & Date:    %s", time);
		write(fd, msg, strlen(msg));
		free(time);

		sleep(3);
		clear_display(fd);
	}

	return 0;
}
