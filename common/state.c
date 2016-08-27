/*
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
#include <string.h>
#include <stdlib.h>
#include "state.h"

const char* states[] = { "unknown", "none", "keyholder", "member", "open", "open+" };

static bool file_write(const char *dir, const char *filename, const char *data) {
	size_t written;
	size_t len = strlen(data);
	FILE *f;

	size_t pathlen = strlen(dir)+strlen(filename)+2;
	char *path = malloc(pathlen);
	snprintf(path, pathlen, "%s/%s", dir, filename);

	f = fopen(path, "w");
	if (!f) {
		free(path);
		return false;
	}
	
	written = fwrite(data, 1, len, f);
	fwrite("\n", 1, 1, f);
	fclose(f);
	free(path);

	return (written >= len);
}

static char* file_read(const char *dir, const char *filename) {
	size_t len = 0;
	char *line = NULL;
	FILE *f;

	size_t pathlen = strlen(dir)+strlen(filename)+2;
	char *path = malloc(pathlen);
	snprintf(path, pathlen, "%s/%s", dir, filename);

	f = fopen(path, "r");
	if (!f) {
		free(path);
		return NULL;
	}

	getline(&line, &len, f);

	fclose(f);
	free(path);

	if (len <= 0) {
		free(line);
		return NULL;
	}

	len = strlen(line);
	if(len>=1 && line[len-1] == '\n')
		line[len-1] = '\0';

	return line;
}

static int safe_atoi(const char *str) {
	if (str)
		return atoi(str);
	else
		return 0;
}

bool state_read(const char *statedir, int *keyholder_id, char **keyholder_name, enum state *status, char **message) {
	char *status_str, *keyholder_id_str;

	if (keyholder_id) {
		keyholder_id_str = file_read(statedir, "keyholder-id");
		*keyholder_id = safe_atoi(keyholder_id_str);
	}

	if (keyholder_name) {
		*keyholder_name = file_read(statedir, "keyholder-name");
	}

	if (status) {
		status_str = file_read(statedir, "status");
		*status = str2state(status_str);
	}

	if (message) {
		*message = file_read(statedir, "message");
	}

	if ((keyholder_id && !keyholder_id_str) || (keyholder_name && !*keyholder_name) || (status && !status_str) || (message && !*message)) {
		if (keyholder_id)
			free(keyholder_id_str);
		if (keyholder_name)
			free(*keyholder_name);
		if (status)
			free(status_str);
		if (message)
			free(*message);

		return false;
	}

	free(keyholder_id_str);
	free(status_str);

	return true;
}

bool state_write(const char *statedir, int keyholder_id, const char *keyholder_name, enum state status, const char *message) {
	char keyholder_id_str[16];
	bool result = true;
	snprintf(keyholder_id_str, sizeof(keyholder_id_str), "%d", keyholder_id);

	result &= file_write(statedir, "keyholder-id", keyholder_id_str);
	result &= file_write(statedir, "keyholder-name", keyholder_name);
	result &= file_write(statedir, "status", state2str(status));
	result &= file_write(statedir, "message", message);

	return result;
}
