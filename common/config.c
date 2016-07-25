/*
 * Access Control System - Config Parser
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
#include <errno.h>

#include "config.h"

FILE *cfg_open() {
	FILE *f;

	f = fopen(CONFIGFILE, "r");
	if (!f) {
		fprintf(stderr, "Could not open config file: %d!\n", errno);
		return NULL;
	}

	return f;
}

void cfg_close(FILE *cfg) {
	if (cfg)
		fclose(cfg);
}

char *cfg_get(FILE *cfg, char *key) {
	char *line = NULL;
	char *lineval; /* pointer to value */
	char *result = NULL;
	size_t key_len = strlen(key);
	size_t len;
	ssize_t read;

	if (!cfg)
		return result;

	fseek(cfg, 0, SEEK_SET);

	while ((read = getline(&line, &len, cfg)) != -1) {
		/* skip lines starting with # (comments) */
		if (!strncmp(line, "#", 1))
			continue;

		/* skip lines not starting with $key */
		if (strncmp(line, key, key_len))
			continue;

		/* variable name may be followed by spaces or tabs */
		for (lineval = line + key_len; *lineval == ' ' || *lineval == '\t' ; lineval++);

		if (*lineval != '=') {
			/* invalid syntax! */
			continue;
		} else
			lineval++;

		/* value may be prefixed by spaces or tabs */
		for (; *lineval == ' ' || *lineval == '\t' ; lineval++);

		result = strndup(lineval, strlen(lineval)-1);
		break;
	}
	free(line);

	return result;
}

int cfg_get_int(FILE *cfg, char *key) {
	char *result = cfg_get(cfg, key);
	if (!result)
		return -1;
	return atoi(result);
}
