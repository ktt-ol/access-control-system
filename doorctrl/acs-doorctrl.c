#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../keyboard/gpio.h"
#include "../common/config.h"
#include "../common/state.h"

#define EVENT_SIZE			( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN		( 1024 * ( EVENT_SIZE + 16 ) )

#define TOPIC_KEYHOLDER_NAME "/access-control-system/keyholder-name"
#define TOPIC_KEYHOLDER_ID   "/access-control-system/keyholder-id"
#define TOPIC_KEYHOLDER_MSG  "/access-control-system/keyholder-message"
#define TOPIC_STATE          "/access-control-system/space-state-test"

struct gpiodesc gpios[] = {
	{ "platform/3f200000.gpio",  8, "cfa1000 unlock", GPIO_OUTPUT, GPIO_ACTIVE_LOW, -1, -1 },
	{ "platform/3f200000.gpio", 11, "cfa1000 lock",   GPIO_OUTPUT, GPIO_ACTIVE_LOW, -1, -1 },
	{ 0 }
};

#define GPIO_LOCK 1
#define GPIO_UNLOCK 0

static struct state_t {
	int keyholder_id;
	char *keyholder_name;
	enum state status;
	char *message;

	int fd;
	int wd;
} state;

void lock() {
	printf("lock!\n");
	gpio_write(&gpios[GPIO_LOCK], true);
	usleep(500000);
	gpio_write(&gpios[GPIO_LOCK], false);
}

void unlock() {
	printf("unlock!\n");
	gpio_write(&gpios[GPIO_UNLOCK], true);
	usleep(500000);
	gpio_write(&gpios[GPIO_UNLOCK], false);
}

int main(int argc, char **argv) {
	int ret, i;

	FILE *cfg = cfg_open();
	char *statedir = cfg_get_default(cfg, "statedir", STATEDIR);
	cfg_close(cfg);

	for (i = 0; gpios[i].dev; i++) {
		int err = gpio_init(&gpios[i]);
		if (err) {
			fprintf(stderr, "could not init gpio \"%s\": %d!\n", gpios[i].name, err);
			return 1;
		}
	}

	char buffer[EVENT_BUF_LEN];

	state.fd = inotify_init();
	if(state.fd < 0) {
		fprintf(stderr, "Could not init inotify!\n");
		return 1;
	}

	ret = mkdir(statedir, 0775);
	if (ret < 0 && errno != EEXIST) {
		fprintf(stderr, "Could not create statedir: %d\n", ret);
		return 1;
	}

	ret = chmod(statedir, 0775);
	if (ret < 0) {
		fprintf(stderr, "Cannot fix rights on statedir: %d\n", ret);
		return 1;
	}

	state.wd = inotify_add_watch(state.fd, statedir, IN_MODIFY);

	for(;;) {
		int len = read(state.fd, buffer, EVENT_BUF_LEN);
		sleep(1);
		len = read(state.fd, buffer, EVENT_BUF_LEN);

		if(!state_read(statedir, &state.keyholder_id, &state.keyholder_name, &state.status, &state.message)) {
			fprintf(stderr, "Could not read state!\n");
			return 1;
		}

		printf("new state: id=%d name=%s status=%d message=%s\n", state.keyholder_id, state.keyholder_name, state.status, state.message);

		switch(state.status) {
			case STATE_NONE:
				lock();
				break;
			case STATE_KEYHOLDER:
			case STATE_MEMBER:
			case STATE_OPEN:
			case STATE_OPEN_PLUS:
				unlock();
				break;
			case STATE_UNKNOWN:
			default:
				break;
		}

		free(state.keyholder_name);
		free(state.message);
	}

	inotify_rm_watch(state.fd, state.wd);
	close(state.fd);
}
