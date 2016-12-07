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

/* for asprintf */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include <sys/inotify.h>
#include "../common/config.h"

#define TOPIC_KEYHOLDER_ID "/access-control-system/keyholder/id"
#define TOPIC_KEYHOLDER_NAME "/access-control-system/keyholder/name"
#define TOPIC_STATE_CUR "/access-control-system/space-state"
#define TOPIC_STATE_NEXT "/access-control-system/space-state-next"
#define TOPIC_MESSAGE "/access-control-system/message"

#define TOPIC_BUZZER_MAIN  "/access-control-system/main-door/buzzer"
#define TOPIC_BUZZER_GLASS "/access-control-system/glass-door/buzzer"

/* check all 10 minutes even witout inotify event */
#define POLL_TIMEOUT 10 * 60 * 1000

/* absolute file path */
struct acs_files {
	char *keyholder_id;
	char *keyholder_name;
	char *status;
	char *status_next;
	char *message;
	char *door_open;
};

/* file content */
struct acs_state {
	char *keyholder_id;
	char *keyholder_name;
	char *status;
	char *status_next;
	char *message;
	char *door_open;
};

int ifd; /* inotify file descriptor */
int wfd; /* directory watch file descriptor */

static void on_connect(struct mosquitto *m, void *udata, int res) {
	printf("Connected to MQTT.\n");
}

static void on_disconnect(struct mosquitto *m, void *udata, int res) {
	fprintf(stderr, "Disconnected from MQTT.\n");
	exit(1);
}

static void on_publish(struct mosquitto *m, void *udata, int m_id) {
#ifdef DEBUG
	printf("Message published.\n");
#endif
}

static void on_log(struct mosquitto *m, void *udata, int level, const char *str) {
#ifdef DEBUG
	printf("[%d] %s\n", level, str);
#endif
}

static int acsf_init(char *statedir, struct acs_files *acsf) {
	int err;

	err = asprintf(&acsf->keyholder_id, "%s/keyholder-id", statedir);
	if (err <= 0)
		return err;
	
	err = asprintf(&acsf->keyholder_name, "%s/keyholder-name", statedir);
	if (err <= 0)
		return err;

	err = asprintf(&acsf->status, "%s/status", statedir);
	if (err <= 0)
		return err;

	err = asprintf(&acsf->status_next, "%s/status-next", statedir);
	if (err <= 0)
		return err;

	err = asprintf(&acsf->message, "%s/message", statedir);
	if (err <= 0)
		return err;

	err = asprintf(&acsf->door_open, "%s/open-door", statedir);
	if (err <= 0)
		return err;

	return 0;
}

static char* file_read_line(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return NULL;
	}

	char *line = NULL;
	size_t len = 0, read;

	read = getline(&line, &len, f);
	if (read < 0) {
		fprintf(stderr, "could not read %s\n", path);
		return NULL;
	}

	/* remove newline */
	line[strlen(line)-1] = '\0';

	fclose(f);

	return line;
}

static int acsf_read(struct acs_files *acsf, struct acs_state *acss) {
	char *line = NULL;
	size_t len = 0, read;

	acss->keyholder_id = file_read_line(acsf->keyholder_id);
	acss->keyholder_name = file_read_line(acsf->keyholder_name);
	acss->status = file_read_line(acsf->status);
	acss->status_next = file_read_line(acsf->status_next);
	acss->message = file_read_line(acsf->message);
	acss->door_open = file_read_line(acsf->door_open);

	if (!acss->keyholder_id || !acss->keyholder_name || !acss->status || !acss->status_next || !acss->message)
		return -1;

	return 0;
}

static bool acs_validate(struct acs_state *a) {
	if (!a)
		return false;
	if (!a->keyholder_id)
		return false;
	if (!a->keyholder_name)
		return false;
	if (!a->status)
		return false;
	if (!a->status_next)
		return false;
	if (!a->message)
		return false;

	return true;
}

static unsigned char acs_cmp(struct acs_state *a, struct acs_state *b) {
	if (!acs_validate(a) && !acs_validate(b))
		return 0;
	if (!acs_validate(a) || !acs_validate(b))
		return 1;

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

static void acs_free(struct acs_state *s) {
	if (s->keyholder_id) {
		free(s->keyholder_id);
		s->keyholder_id = NULL;
	}
	if (s->keyholder_name) {
		free(s->keyholder_name);
		s->keyholder_name = NULL;
	}
	if (s->status) {
		free(s->status);
		s->status = NULL;
	}
	if (s->status_next) {
		free(s->status_next);
		s->status_next = NULL;
	}
	if (s->message) {
		free(s->message);
		s->message = NULL;
	}
	if (s->door_open) {
		free(s->door_open);
		s->door_open = NULL;
	}
}

static void handle_inotify(int fd) {
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *event;
	int i;
	ssize_t len;
	char *ptr;

	for (;;) {
		/* Read some events. */
		len = read(fd, buf, sizeof buf);
		if (len == -1 && errno != EAGAIN) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		/* If the nonblocking read() found no events to read, then
		it returns -1 with errno set to EAGAIN. In that case,
		we exit the loop. */
		if (len <= 0)
			break;

		/* Loop over all events in the buffer */
		printf("modified files: ");
		for (ptr = buf; ptr < buf + len;
			ptr += sizeof(struct inotify_event) + event->len) {

			event = (const struct inotify_event *) ptr;

			/* Print the name of the file */
			if (event->len)
				printf("%s ", event->name);
		}

		printf("\n");
	}
}

int main(int argc, char **argv) {
	struct mosquitto *mosq;
	struct acs_files acsf;
	struct acs_state acss = {
		.keyholder_id = strdup(""),
		.keyholder_name = strdup(""),
		.status = strdup(""),
		.status_next = strdup(""),
		.message = strdup(""),
	};
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
	mosquitto_disconnect_callback_set(mosq, on_disconnect);
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
		printf("Could not init acsf!\n");
		return 1;
	}

	ifd = inotify_init1(IN_NONBLOCK);
	if (ifd == -1)
		return 1;

	printf("Watched state-directory: %s\n", statedir);
	wfd = inotify_add_watch(ifd, statedir, IN_MODIFY | IN_DELETE);
	if (wfd == -1)
		return 1;

	struct pollfd fdset[1];
	fdset[0].fd = ifd;
	fdset[0].events = POLLIN;

	ret = mosquitto_loop_start(mosq);
	if (ret) {
		fprintf(stderr, "Error could not start mosquitto network loop: %d\n", ret);
		return 1;
	}

	for (;;) {
		struct acs_state newacss;

		ret = acsf_read(&acsf, &newacss);
		if (ret < 0) {
				fprintf(stderr, "failed to read state: %d\n", ret);
				acs_free(&newacss);

				/* invalidate cached information */
				acs_free(&acss);
		} else { 
			if(acs_cmp(&acss, &newacss)) {
				acs_free(&acss);
				acss = newacss;

				printf("Publish new state:\n");
				printf("  keyholder:   %s (%s)\n", newacss.keyholder_name, newacss.keyholder_id);
				printf("  status:      %s\n", newacss.status);
				printf("  status-next: %s\n", newacss.status_next[0] == '\0' ? "--- unset ---" : newacss.status_next);
				printf("  message:     %s\n", newacss.message[0] == '\0' ? "--- unset ---" : newacss.message);

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
			} else if(newacss.door_open) {
				/* remove file */
				unlink(acsf.door_open);

				if (!strcmp(newacss.door_open, "glass")) {
					printf("door open: glass\n");

					ret = mosquitto_publish(mosq, NULL, TOPIC_BUZZER_GLASS, 2, "1", 0, true);
					if (ret) {
						fprintf(stderr, "Error could not send message: %d\n", ret);
						return 1;
					}

					sleep(3);

					ret = mosquitto_publish(mosq, NULL, TOPIC_BUZZER_GLASS, 2, "0", 0, true);
					if (ret) {
						fprintf(stderr, "Error could not send message: %d\n", ret);
						return 1;
					}
				} else if (!strcmp(newacss.door_open, "main")) {
					printf("door open: main\n");

					ret = mosquitto_publish(mosq, NULL, TOPIC_BUZZER_MAIN, 2, "1", 0, true);
					if (ret) {
						fprintf(stderr, "Error could not send message: %d\n", ret);
						return 1;
					}

					sleep(3);

					ret = mosquitto_publish(mosq, NULL, TOPIC_BUZZER_MAIN, 2, "0", 0, true);
					if (ret) {
						fprintf(stderr, "Error could not send message: %d\n", ret);
						return 1;
					}
				} else {
					fprintf(stderr, "Could not open unknown door: %s\n", newacss.door_open);
				}

				acs_free(&newacss);
			} else {
				printf("Not publishing unchanged state!\n");
				acs_free(&newacss);
			}
		}

		ret = poll(fdset, 1, POLL_TIMEOUT);
		if (ret < 0) {
				fprintf(stderr, "Failed to poll: %d\n", ret);
				return 1;
		}

		/* wait until all files have been written to reduce MQTT spam  */
		sleep(1);

		if (fdset[0].revents & POLLIN)
			handle_inotify(ifd);
	}

	/* free config values */
	free(statedir);
	free(host);
	free(cert);
	free(user);
	free(pass);

	/* cleanup */
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return 0;
}
