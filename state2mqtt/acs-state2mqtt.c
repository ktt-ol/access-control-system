#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <mosquitto.h>
#include "../common/config.h"
#include "../common/state.h"

#define EVENT_SIZE			( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN		( 1024 * ( EVENT_SIZE + 16 ) )

#define TOPIC_KEYHOLDER_NAME "/access-control-system/keyholder-name"
#define TOPIC_KEYHOLDER_ID   "/access-control-system/keyholder-id"
#define TOPIC_KEYHOLDER_MSG  "/access-control-system/keyholder-message"
#define TOPIC_STATE          "/access-control-system/space-state-test"

static struct state_t {
	int keyholder_id;
	char *keyholder_name;
	enum state status;
	char *message;

	int fd;
	int wd;
} state;

static void on_connect(struct mosquitto *m, void *udata, int res) {
	fprintf(stderr, "Connected.\n");
}

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
	fprintf(stdout, "Message published.\n");
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stdout, "[%d] %s\n", level, str);
}

static int publish_state(struct mosquitto *mosq) {
	char keyholder_id[16];
	const char *statename;
	int ret;

	printf("Publishing new space status via MQTT\n");
	printf("  Keyholder: %s (%d)\n", state.keyholder_name, state.keyholder_id);
	printf("  Status: %s\n", state2str(state.status));
	printf("  Message: %s\n", state.message);

	ret = mosquitto_publish(mosq, NULL, TOPIC_KEYHOLDER_NAME, strlen(state.keyholder_name), state.keyholder_name, 0, true);
	if (ret) {
		fprintf(stderr, "Error could not send message: %d\n", ret);
		return 1;
	}

	snprintf(keyholder_id, 16, "%d", state.keyholder_id);
	ret = mosquitto_publish(mosq, NULL, TOPIC_KEYHOLDER_ID, strlen(keyholder_id), keyholder_id, 0, true);
	if (ret) {
		fprintf(stderr, "Error could not send message: %d\n", ret);
		return 1;
	}

	ret = mosquitto_publish(mosq, NULL, TOPIC_KEYHOLDER_MSG, strlen(state.message), state.message, 0, true);
	if (ret) {
		fprintf(stderr, "Error could not send message: %d\n", ret);
		return 1;
	}

	statename = state2str(state.status);
	ret = mosquitto_publish(mosq, NULL, TOPIC_STATE, strlen(statename), statename, 0, true);
	if (ret) {
		fprintf(stderr, "Error could not send message: %d\n", ret);
		return 1;
	}

	return 0;
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	int ret;

	mosquitto_lib_init();

	FILE *cfg = cfg_open();
	char *statedir = cfg_get_default(cfg, "statedir", STATEDIR);

	char *user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	char *pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	char *cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
	char *host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
	int port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	int keepalv = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);

	cfg_close(cfg);

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

	/* create mosquitto client instance */
	mosq = mosquitto_new("state2mqtt", true, NULL);
	if(!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_publish_callback_set(mosq, on_publish);
	mosquitto_log_callback_set(mosq, on_log);

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

	/* mqtt background handling */
	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	for(;;) {
		int len = read(state.fd, buffer, EVENT_BUF_LEN);
		sleep(1);
		len = read(state.fd, buffer, EVENT_BUF_LEN);

		if(!state_read(statedir, &state.keyholder_id, &state.keyholder_name, &state.status, &state.message)) {
			fprintf(stderr, "Could not read state!\n");
			return 1;
		}

		ret = publish_state(mosq);
		if (ret)
			return 1;

		free(state.keyholder_name);
		free(state.message);
	}

	inotify_rm_watch(state.fd, state.wd);
	close(state.fd);
}
