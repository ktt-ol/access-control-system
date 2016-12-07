#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <mosquitto.h>
#include "../common/config.h"

#define STATE_TOPIC "/access-control-system/space-state"
const static char* states[] = {
	"unknown",
	"disconnected",
	"none",
	"keyholder",
	"member",
	"open",
	"open+",
};

/* order should match states[] */
enum states2 {
	STATE_UNKNOWN,
	STATE_DISCONNECTED,
	STATE_NONE,
	STATE_KEYHOLDER,
	STATE_MEMBER,
	STATE_OPEN,
	STATE_OPEN_PLUS,
	STATE_MAX,
};

struct userdata {
	struct mosquitto *mosq;
	enum states2 state;
};

#define display_size 32+1

const static char clear_display_cmd[] = {0xfe, 0x01};
#define clear_display(fd) write(fd, clear_display_cmd, sizeof(clear_display_cmd));

const static char display_backlight_enable_cmd[] = {0x7c, 157};
#define display_backlight_enable(fd) write(fd, display_backlight_enable_cmd, sizeof(display_backlight_enable_cmd));

const static char display_backlight_disable_cmd[] = {0x7c, 128};
#define display_backlight_disable(fd) write(fd, display_backlight_disable_cmd, sizeof(display_backlight_disable_cmd));


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

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}

static void on_subscribe(struct mosquitto *m, void *udata, int mid, int qos_count, const int *granted_qos) {
	int i;

	fprintf(stderr, "MQTT: Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++) {
		fprintf(stderr, ", %d", granted_qos[i]);
	}

	fprintf(stderr, "\n");
}

static void on_connect(struct mosquitto *m, void *data, int res) {
	int ret;
	struct userdata *udata = (struct userdata*) data;

	fprintf(stderr, "MQTT Connected.\n");

	ret = mosquitto_subscribe(m, NULL, STATE_TOPIC, 1);
	if (ret) {
		fprintf(stderr, "MQTT Error: Could not subscribe to %s: %d\n", STATE_TOPIC, ret);
		exit(1);
	}
}

static void on_disconnect(struct mosquitto *m, void *data, int res) {
	struct userdata *udata = (struct userdata*) data;

	/* ignore repeated events */
	if(udata->state == STATE_DISCONNECTED)
		return;

	udata->state = STATE_DISCONNECTED;

	fprintf(stderr, "MQTT Disconnected.\n");
}

static void on_message(struct mosquitto *m, void *data, const struct mosquitto_message *msg) {
	int i;
	enum states2 curstate = STATE_UNKNOWN;
	struct userdata *udata = (struct userdata*) data;

	/* wrong */
	if(strcmp(STATE_TOPIC, msg->topic)) {
		fprintf(stderr, "Ignored message with wrong topic\n");
		return;
	}

	for(i=0; i < STATE_MAX; i++) {
		if(!strncmp(states[i], msg->payload, msg->payloadlen)) {
			curstate = i;
			break;
		}
	}

	if(curstate == STATE_UNKNOWN) {
		char *m = strndup(msg->payload, msg->payloadlen);
		fprintf(stderr, "Incorrect state received: %s\n", m);
		free(m);
	}

	fprintf(stderr, "MQTT state change: %s\n", states[curstate]);

	udata->state = curstate;
}

static int mosquitto_init(struct userdata *udata) {
	struct mosquitto *mosq;
	int ret;

	udata->state = STATE_UNKNOWN;

	/* load mosquitto config */
	FILE *cfg = cfg_open();
	char *user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	char *pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	char *cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
	char *host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
	int port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	int keepalv = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);
	cfg_close(cfg);

	mosquitto_lib_init();

	mosq = mosquitto_new("space-status-display", true, udata);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	udata->mosq = mosq;

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);
	mosquitto_log_callback_set(mosq, on_log);
	mosquitto_subscribe_callback_set(mosq, on_subscribe);
	mosquitto_message_callback_set(mosq, on_message);

	/* setup credentials */
	if (strcmp(user, "")) {
		ret = mosquitto_username_pw_set(mosq, user, pass);
		if(ret) {
			fprintf(stderr, "Error setting credentials: %d\n", ret);
			return 1;
		}
	}

	if (strcmp(cert, "")) {
		ret = mosquitto_tls_set(mosq, cert, NULL, NULL, NULL, NULL);
		if(ret) {
			fprintf(stderr, "Error setting TLS mode: %d\n", ret);
			return 1;
		}

		ret = mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
		if(ret) {
			fprintf(stderr, "Error requiring TLS 1.2: %d\n", ret);
			return 1;
		}
	}

	/* connect to broker */
	ret = mosquitto_connect(mosq, host, port, keepalv);
	if (ret) {
		fprintf(stderr, "Error could not connect to broker: %d\n", ret);
		return 1;
	}

	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto loop: %d\n", ret);
		return 1;
	}
}

int main(int argc, char **argv) {
	FILE *cfg = cfg_open();
	char *portname = cfg_get_default(cfg, "serial-display-dev", SERIAL_DISPLAY_DEV);
	char *ethdev = cfg_get_default(cfg, "network-dev", NETWORK_DEV);
	cfg_close(cfg);
	char *msg = malloc(display_size);
	char *ip, *time;
	struct userdata *mqttdata;
	int err;

	int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
	if(fd < 0) {
		fprintf(stderr, "Could not open serial device!\n");
		return 1;
	}

	serial_setup(fd, B9600);
	clear_display(fd);
	display_backlight_enable(fd);

	fprintf(stderr, "Display initialized!\n");

	mqttdata = malloc(sizeof(*mqttdata));
	if(!mqttdata) {
		fprintf(stderr, "out of memory!\n");
		return 1;
	}

	err = mosquitto_init(mqttdata);
	if (err)
		return err;

	while(1) {
		snprintf(msg, display_size, "Spaceschalter   3.0");
		write(fd, msg, strlen(msg));

		sleep(3);
		clear_display(fd);

		if(get_carrier(ethdev)) {
			ip = get_ip_addr(ethdev);
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

		snprintf(msg, display_size, "Space Status: %18s", states[mqttdata->state]);
		write(fd, msg, strlen(msg));

		sleep(3);
		clear_display(fd);
	}

	return 0;
}
