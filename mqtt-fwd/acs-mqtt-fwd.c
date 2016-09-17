/*
 * MQTT Forwarder
 *
 * Copyright (c) 2015, Sebastian Reichel <sre@mainframe.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include "../common/config.h"

#define TOPIC_KEYHOLDER_ID "/access-control-system/keyholder/id"
#define TOPIC_KEYHOLDER_NAME "/access-control-system/keyholder/name"
#define TOPIC_STATE_CUR "/access-control-system/space-state"
#define TOPIC_STATE_NEXT "/access-control-system/space-state-next"
#define TOPIC_MESSAGE "/access-control-system/message"

#define FILE_TIMEOUT 30

struct acs_files {
	/* file handles */
	FILE *keyholder_id;
	FILE *keyholder_name;
	FILE *status;
	FILE *status_next;
	FILE *message;

	struct pollfd fdset[5];
};

struct acs_state {
	char *keyholder_id;
	char *keyholder_name;
	char *status;
	char *status_next;
	char *message;
};

static void on_connect(struct mosquitto *m, void *udata, int res) {
	printf("Connected.\n");
}

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
	printf("Message published.\n");
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
	fprintf(stderr, "[%d] %s\n", level, str);
}

static int acsf_init(char *statedir, struct acs_files *acsf) {
	size_t pathlen = strlen(statedir) + 16;
	char *path = malloc(pathlen);

	snprintf(path, pathlen, "%s/keyholder-id", statedir);
	acsf->keyholder_id = fopen(path, "r");
	if (!acsf->keyholder_id) {
		fprintf(stderr, "could not open keyholder-id state file");
		goto fail1;
	}

	snprintf(path, pathlen, "%s/keyholder-name", statedir);
	acsf->keyholder_name = fopen(path, "r");
	if (!acsf->keyholder_name) {
		fprintf(stderr, "could not open keyholder-name state file");
		goto fail2;
	}

	snprintf(path, pathlen, "%s/status", statedir);
	acsf->status = fopen(path, "r");
	if (!acsf->status) {
		fprintf(stderr, "could not open space-status state file");
		goto fail3;
	}

	snprintf(path, pathlen, "%s/status-next", statedir);
	acsf->status_next = fopen(path, "r");
	if (!acsf->status_next) {
		fprintf(stderr, "could not open space-status-next state file");
		goto fail4;
	}

	snprintf(path, pathlen, "%s/message", statedir);
	acsf->message = fopen(path, "r");
	if (!acsf->message) {
		fprintf(stderr, "could not open space-message state file");
		goto fail5;
	}

	free(path);
	free(statedir);

	acsf->fdset[0].fd = fileno(acsf->keyholder_id);
	acsf->fdset[0].events = POLLPRI;
	acsf->fdset[1].fd = fileno(acsf->keyholder_name);
	acsf->fdset[1].events = POLLPRI;
	acsf->fdset[2].fd = fileno(acsf->status);
	acsf->fdset[2].events = POLLPRI;
	acsf->fdset[3].fd = fileno(acsf->status_next);
	acsf->fdset[3].events = POLLPRI;
	acsf->fdset[4].fd = fileno(acsf->message);
	acsf->fdset[4].events = POLLPRI;

	return 0;

#if 0
fail6:
	fclose(acsf->message);
#endif
fail5:
	fclose(acsf->status_next);
fail4:
	fclose(acsf->status);
fail3:
	fclose(acsf->keyholder_name);
fail2:
	fclose(acsf->keyholder_id);
fail1:
	return errno;
}

static int acsf_read(struct acs_files *acsf, struct acs_state *acss) {
	char *line = NULL;
	size_t len = 0, read;

	read = getline(&line, &len, acsf->keyholder_id);
	if (read > 0)
		return -1;
	acss->keyholder_id = strndup(line, strlen(line)-1);

	read = getline(&line, &len, acsf->keyholder_name);
	if (read > 0)
		return -1;
	acss->keyholder_name = strndup(line, strlen(line)-1);

	read = getline(&line, &len, acsf->status);
	if (read > 0)
		return -1;
	acss->status = strndup(line, strlen(line)-1);

	read = getline(&line, &len, acsf->status_next);
	if (read > 0)
		return -1;
	acss->status_next = strndup(line, strlen(line)-1);

	read = getline(&line, &len, acsf->message);
	if (read > 0)
		return -1;
	acss->message = strndup(line, strlen(line)-1);

	return -1;
}

static unsigned char acs_cmp(struct acs_state *a, struct acs_state *b) {
	if (strcmp(a->keyholder_id, b->keyholder_id))
		return 1;
	if (strcmp(a->keyholder_name, b->keyholder_name))
		return 1;
	if (strcmp(a->status, b->status))
		return 1;
	if (strcmp(a->status_next, b->status_next))
		return 1;
	if (strcmp(a->message, b->message))
		return 1;
	
	return 0;
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	struct acs_files acsf;
	struct acs_state acss = {};
	int ret;

	mosquitto_lib_init();

	/* create mosquitto client instance */
	mosq = mosquitto_new("access-control-system", true, NULL);
	if (!mosq) {
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	/* get configuration */
	FILE *cfg = cfg_open();;
	char *statedir = cfg_get_default(cfg, "statedir", STATEDIR);
	char *host = cfg_get_default(cfg, "mqtt-broker-host", MQTT_BROKER_HOST);
	int port = cfg_get_int_default(cfg, "mqtt-broker-port", MQTT_BROKER_PORT);
	char *cert = cfg_get_default(cfg, "mqtt-broker-cert", MQTT_BROKER_CERT);
	char *user = cfg_get_default(cfg, "mqtt-username", MQTT_USERNAME);
	char *pass = cfg_get_default(cfg, "mqtt-password", MQTT_PASSWORD);
	int keepalive = cfg_get_int_default(cfg, "mqtt-keepalive", MQTT_KEEPALIVE_SECONDS);
	cfg_close(cfg);

	/* setup callbacks */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_publish_callback_set(mosq, on_publish);
	mosquitto_log_callback_set(mosq, on_log);

	/* setup credentials */
	if (strcmp(user, "")) {
		ret = mosquitto_username_pw_set(mosq, user, pass);
		if (ret) {
			fprintf(stderr, "Error setting credentials: %d\n", ret);
			return 1;
		}
	}

	/* setup tls */
	if (strcmp(cert, "")) {
		ret = mosquitto_tls_set(mosq, cert, NULL, NULL, NULL, NULL);
		if (ret) {
			fprintf(stderr, "Error setting TLS mode: %d\n", ret);
			return 1;
		}

		ret = mosquitto_tls_opts_set(mosq, 1, "tlsv1.2", NULL);
		if (ret) {
			fprintf(stderr, "Error requiring TLS 1.2: %d\n", ret);
			return 1;
		}
	}

	/* connect to broker */
	ret = mosquitto_connect(mosq, host, port, keepalive);
	if (ret) {
		fprintf(stderr, "Error could not connect to broker: %d\n", ret);
		return 1;
	}

	/* open state files */
	ret = acsf_init(statedir, &acsf);
	if (ret) {
		return 1;
	}

	/* free config values */
	free(host);
	free(cert);
	free(user);
	free(pass);
	free(statedir);

	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	for (;;) {
		struct acs_state newacss;

		ret = poll(acsf.fdset, 4, FILE_TIMEOUT);
		if (ret < 0) {
				fprintf(stderr, "Failed to poll gpios: %d\n", ret);
				return 1;
		}

		ret = acsf_read(&acsf, &newacss);
		if (ret < 0) {
				fprintf(stderr, "failed to read state: %d\n", ret);
				return 1;
		}

		if(acs_cmp(&acss, &newacss)) {
			acss = newacss;

			/* publish state */
			ret = mosquitto_publish(mosq, NULL, TOPIC_KEYHOLDER_ID, strlen(acss.keyholder_id), acss.keyholder_id, 0, true);
			if (ret) {
				fprintf(stderr, "Error could not send message: %d\n", ret);
				return 1;
			}

			ret = mosquitto_publish(mosq, NULL, TOPIC_KEYHOLDER_NAME, strlen(acss.keyholder_name), acss.keyholder_name, 0, true);
			if (ret) {
				fprintf(stderr, "Error could not send message: %d\n", ret);
				return 1;
			}

			ret = mosquitto_publish(mosq, NULL, TOPIC_STATE_CUR, strlen(acss.status), acss.status, 0, true);
			if (ret) {
				fprintf(stderr, "Error could not send message: %d\n", ret);
				return 1;
			}

			ret = mosquitto_publish(mosq, NULL, TOPIC_STATE_NEXT, strlen(acss.status_next), acss.status_next, 0, true);
			if (ret) {
				fprintf(stderr, "Error could not send message: %d\n", ret);
				return 1;
			}

			ret = mosquitto_publish(mosq, NULL, TOPIC_MESSAGE, strlen(acss.message), acss.message, 0, true);
			if (ret) {
				fprintf(stderr, "Error could not send message: %d\n", ret);
				return 1;
			}
		}

		sleep(FILE_TIMEOUT);
	}

	/* cleanup */
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return 0;
}
