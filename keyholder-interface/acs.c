/*
 * Access Control System
 *
 * Copyright (c) 2015-2016, Sebastian Reichel <sre@mainframe.io>
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

#define _GNU_SOURCE /* asprintf is specific to GNU and *BSD based systems */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pcre.h>
#include <time.h>
#include <sqlite3.h>
#include <openssl/evp.h>
#include <systemd/sd-journal.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "../common/config.h"

#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

#define SSHDNAME "sshd"

static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static const char* modes[] = { "unknown", "none", "keyholder", "member", "open", "open+" };

/* (Month) (Day) (Hour):(Minute):(Second) (Hostname) (Processname)[(Processid)]: (Message) */
//static const char *LOG_REGEX = "^([a-z]{3}) ([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2}) ([^ ]+) ([a-zA-Z]+)\\[([0-9]+)\\]: (.*)$";
static const char *LOG_REGEX = "^([A-Z][a-z]{2}) ([ 0-3][0-9]) ([0-9]{2}):([0-9]{2}):([0-9]{2}) ([^ ]+) ([a-zA-Z]+)\\[([0-9]+)\\]: (.*)$";

// ... (username) ... (ip) ... (keytype) ... (keyhash)
static const char *SSH_REGEX = "^Accepted publickey for ([-_\\.a-zA-Z0-9]+) from ([0-9\\.]+) port [0-9]+ ssh2: ([A-Z0-9]+) ([0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2})$";

FILE *cfg;

static pid_t process_get_parent(pid_t pid) {
	char path[32];
	FILE *f;
	char *line = NULL;
	size_t len;
	ssize_t read;

	snprintf(path, sizeof(path), "/proc/%d/status", pid);

	f = fopen(path, "r");
	if (!f)
		return 0;

	while ((read = getline(&line, &len, f)) != -1) {
		if (!strncmp("PPid:\t", line, 6)) {
			char *tmp = line + 6;
			pid_t result = atoi(tmp);
			fclose(f);
			free(line);
			return result;
		}
	}

	free(line);
	fclose(f);
	return 0;
}

static char* process_get_name(pid_t pid) {
	char path[32];
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	snprintf(path, sizeof(path), "/proc/%d/status", pid);

	f = fopen(path, "r");
	if (!f)
		return NULL;

	while ((read = getline(&line, &len, f)) != -1) {
		if (!strncmp("Name:\t", line, 6)) {
			char *tmp = line + 6;
			char *result = strdup(tmp);
			result[read-7] = '\0';
			fclose(f);
			free(line);
			return result;
		}
	}

	free(line);
	fclose(f);
	return NULL;
}


static bool find_sshd_parent(pid_t *pid) {
	char *name;
	pid_t p;

	for (p = getpid(); p > 1; p = process_get_parent(p)) {
		name = process_get_name(p);

		if (!strcmp(name, SSHDNAME)) {
			*pid = (getuid() == 0) ? p : process_get_parent(p);
			free(name);
			return true;
		}

		free(name);
	}

	fprintf(stderr, "parent ssh daemon not found!\n");
	return false;
}

static bool parse_sshd_message(const char *msg, char **ip, char **type, char **fp) {
	pcre *regex;
	pcre_extra *regex2;
	const char *pcreErrorStr;
	int pcreErrorOffset;
	int subStrVec[15];
	int err;
	const char *submatch;

	regex = pcre_compile(SSH_REGEX, 0, &pcreErrorStr, &pcreErrorOffset, NULL);
	if (!regex) {
		fprintf(stderr, "Could not compile regex: %s\n", pcreErrorStr);
		return false;
	}

	regex2 = pcre_study(regex, 0, &pcreErrorStr);
	if (pcreErrorStr) {
		fprintf(stderr, "Could not study regex: %s\n", pcreErrorStr);
		goto error;
	}

	err = pcre_exec(regex, regex2, msg, strlen(msg), 0, 0, subStrVec, 15);
	if (err < 0) {
		switch(err) {
		case PCRE_ERROR_NOMATCH:
			break;
		case PCRE_ERROR_NULL:
			fprintf(stderr, "Regex Error: NULL!\n");
			break;
		case PCRE_ERROR_BADOPTION:
			fprintf(stderr, "Regex Error: Bad Option!\n");
			break;
		case PCRE_ERROR_BADMAGIC:
			fprintf(stderr, "Regex Error: Bad Magic Number!\n");
			break;
		case PCRE_ERROR_UNKNOWN_NODE:
			fprintf(stderr, "Regex Error: Unknown Node!\n");
			break;
		case PCRE_ERROR_NOMEMORY:
			fprintf(stderr, "Regex Error: Out of memory!\n");
			break;
		default:
			fprintf(stderr, "Regex Error: Unknown!\n");
			break;
		}

		goto error;
	}

	if (err == 0) {
		fprintf(stderr, "Regex Error: Too many substrings!\n");
		goto error;
	}

	if (err != 5) {
		fprintf(stderr, "Regex Error: Incorrect number of substrings!\n");
		goto error;
	}

	pcre_get_substring(msg, subStrVec, err, 2, &(submatch));
	*ip = strdup(submatch);
	pcre_free_substring(submatch);

	pcre_get_substring(msg, subStrVec, err, 3, &(submatch));
	*type = strdup(submatch);
	pcre_free_substring(submatch);

	pcre_get_substring(msg, subStrVec, err, 4, &(submatch));
	*fp = strdup(submatch);
	pcre_free_substring(submatch);

	pcre_free(regex);
	if (regex2)
		pcre_free(regex2);

	return true;

error:
	pcre_free(regex);
	if (regex2)
		pcre_free(regex2);

	return false;
}

static bool log_get_fingerprint(pid_t pid, time_t *logtime, char **ip, char **type, char **fp) {
	FILE *f;
	char *line = NULL;
	size_t len;
	ssize_t read;
	const char *pcreErrorStr;
	int pcreErrorOffset;
	int subStrVec[30];
	pcre *regex;
	pcre_extra *regex2;
	int err;
	const char *submatch;
	struct tm *timedate;
	time_t rawtime;
	pid_t logpid;

	*logtime = 0;

	regex = pcre_compile(LOG_REGEX, 0, &pcreErrorStr, &pcreErrorOffset, NULL);
	if (!regex) {
		fprintf(stderr, "Could not compile regex: %s\n", pcreErrorStr);
		return false;
	}

	regex2 = pcre_study(regex, 0, &pcreErrorStr);
	if (pcreErrorStr) {
		fprintf(stderr, "Could not study regex: %s\n", pcreErrorStr);
		return false;
	}

	char *logfile = cfg_get_default(cfg, "ssh-logfile", strdup(SSHLOGFILE));
	f = fopen(logfile, "r");
	free(logfile);
	if (!f) {
		fprintf(stderr, "could not open auth.log, errno=%d!\n", errno);
		return false;
	}

	while ((read = getline(&line, &len, f)) != -1) {
		err = pcre_exec(regex, regex2, line, read, 0, 0, subStrVec, 30);
		if (err < 0) {
			switch(err) {
			case PCRE_ERROR_NOMATCH:
				continue;
			case PCRE_ERROR_NULL:
				fprintf(stderr, "Regex Error: NULL!\n");
				break;
			case PCRE_ERROR_BADOPTION:
				fprintf(stderr, "Regex Error: Bad Option!\n");
				break;
			case PCRE_ERROR_BADMAGIC:
				fprintf(stderr, "Regex Error: Bad Magic Number!\n");
				break;
			case PCRE_ERROR_UNKNOWN_NODE:
				fprintf(stderr, "Regex Error: Unknown Node!\n");
				break;
			case PCRE_ERROR_NOMEMORY:
				fprintf(stderr, "Regex Error: Out of memory!\n");
				break;
			default:
				fprintf(stderr, "Regex Error: Unknown!\n");
				break;
			}

			continue;
		}

		if (err == 0) {
			fprintf(stderr, "Regex Error: Too many substrings!\n");
			continue;
		}

		if (err != 10) {
			fprintf(stderr, "Regex Error: Incorrect number of substrings!\n");
			continue;
		}

		/* --- regex match found! --- */
		pcre_get_substring(line, subStrVec, err, 7, &(submatch));
		if (strcmp(SSHDNAME, submatch)) {
			pcre_free_substring(submatch);
			continue;
		}
		pcre_free_substring(submatch);

		pcre_get_substring(line, subStrVec, err, 8, &(submatch));
		logpid = atoi(submatch);
		pcre_free_substring(submatch);
		if (logpid != pid)
			continue;

		/* extract time information */
		time(&rawtime);
		timedate = localtime(&rawtime);

		timedate->tm_mon = 12;
		pcre_get_substring(line, subStrVec, err, 1, &(submatch));
		for (int i=0; i < 12; i++) {
			if (!strcmp(submatch, months[i])) {
				timedate->tm_mon = i;
				break;
			}
		}
		pcre_free_substring(submatch);

		pcre_get_substring(line, subStrVec, err, 2, &(submatch));
		timedate->tm_mday = atoi(submatch);
		pcre_free_substring(submatch);

		pcre_get_substring(line, subStrVec, err, 3, &(submatch));
		timedate->tm_hour = atoi(submatch);
		pcre_free_substring(submatch);

		pcre_get_substring(line, subStrVec, err, 4, &(submatch));
		timedate->tm_min = atoi(submatch);
		pcre_free_substring(submatch);

		pcre_get_substring(line, subStrVec, err, 5, &(submatch));
		timedate->tm_sec = atoi(submatch);
		pcre_free_substring(submatch);

		rawtime = mktime(timedate);

		pcre_get_substring(line, subStrVec, err, 9, &(submatch));
		if(parse_sshd_message(submatch, ip, type, fp))
			*logtime = rawtime;
		pcre_free_substring(submatch);
	}
	free(line);

	pcre_free(regex);
	if (regex2)
		pcre_free(regex2);
	fclose(f);

	if (*logtime != 0) {
		return true;
	} else {
		fprintf(stderr, "Could not find login process in auth.log\n");
		return false;
	}
}

static char* key2fp(const char *key_base64) {
	size_t key_base64_len = strlen(key_base64);
	unsigned char *key_raw = (unsigned char *) malloc(key_base64_len);
	size_t key_raw_len = 0;
	EVP_MD_CTX *mdctx = EVP_MD_CTX_create();
	unsigned char md_value[EVP_MAX_MD_SIZE];
	unsigned int md_len;
	int err;
	char *result;

	key_raw_len = EVP_DecodeBlock(key_raw, (unsigned char *) key_base64, key_base64_len); 
	for(int i=key_base64_len-1; key_base64[i] == '='; i--)
		key_raw_len--;

	EVP_MD_CTX_init(mdctx);
	EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);

	err = EVP_DigestUpdate(mdctx, key_raw, key_raw_len);
	if (err != 1)
		return NULL;

	err = EVP_DigestFinal_ex(mdctx, md_value, &md_len);
	if (err != 1)
		return NULL;
	
	EVP_MD_CTX_destroy(mdctx);

	result = malloc(16*3);
	if (!result)
		return NULL;
	
	snprintf(result, 16*3, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		md_value[0], md_value[1], md_value[2], md_value[3], md_value[4], md_value[5], md_value[6], md_value[7],
		md_value[8], md_value[9], md_value[10], md_value[11], md_value[12], md_value[13], md_value[14], md_value[15]);
	
	return result;
}

static bool authorized_keys_get(char *keyfp, char **key, char **comment) {
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	char *kd, *kc;

	char *keyfile = cfg_get_default(cfg, "ssh-keyfile", strdup(SSHKEYFILE));
	f = fopen(keyfile, "r");
	free(keyfile);
	if (!f) {
		fprintf(stderr, "Could not open keyfile!\n");
		return false;
	}

	// Format: "(type) (base64'd pubkey) (comment)"
	while ((read = getline(&line, &len, f)) != -1) {
		/* skip keytype */
		kd = strchr(line, ' ');
		if (kd == NULL) {
			fprintf(stderr, "Malformed authorized_keys file!\n");
			continue;
		}
		kd++;

		kc = strchr(kd, ' ');
		if (!kc) {
			fprintf(stderr, "Malformed authorized_keys file!\n");
			continue;
		}
		*kc = '\0';
		kc++;

		char *fp = key2fp(kd);
		if (strcmp(keyfp, fp)) {
			/* fingerprint mismatch */
			continue;
		}
		free(fp);

		*key = strdup(kd);
		*comment = strndup(kc, strlen(kc)-1);

		free(line);
		fclose(f);
		return true;
	}

	free(line);
	fclose(f);
	fprintf(stderr, "Could not find fingerprint in authorized_keys file!\n");
	return false;
}

static char *keycomment2username(const char *comment) {
	char *split = strchr(comment, '@');

	/* no '@' found, just use whole comment */
	if (!split)
		return strdup(comment);
	
	return strndup(comment, split-comment);
}

unsigned int str2mode(const char *mode) {
	for(unsigned int i=0; i < ARRAYSIZE(modes); i++) {
		if(!strcmp(mode, modes[i]))
			return i;
	}

	return 0;
}

enum cmd {
	CMD_INVALID = 0,
	CMD_SET_STATUS,
	CMD_SET_NEXT_STATUS,
	CMD_OPEN_DOOR,
	CMD_MAX
};

static const char* cmd_str[] = {
	"invalid",
	"set-status",
	"set-next-status",
	"open-door",
};

static enum cmd get_command(char **command) {
	int i;
	enum cmd result = CMD_INVALID;

	if (*command == NULL)
		return CMD_INVALID;
	
	for (i=0; i < CMD_MAX; i++) {
		if (!strncmp(*command, cmd_str[i], strlen(cmd_str[i]))) {
			result = i;
			*command += strlen(cmd_str[i]);
			break;
		}
	}

	if (**command == ' ')
		(*command)++;
	else if(**command != '\0') {
		result = CMD_INVALID;
	}

	return result;
}

static bool parse_status_cmd(char *command, int *mode, char **msg) {
	char *arg, *tmp;

	tmp = strchr(command, ' ');
	if (tmp)
		arg = strndup(command, tmp-command);
	else
		arg = strdup(command);

	/* supplied mode does not exist */
	*mode = str2mode(arg);
	if (*mode <= 0 || *mode >= ARRAYSIZE(modes)) {
		fprintf(stderr, "Invalid Mode: %s\n", arg);
		fprintf(stderr, "Possible modes:\n");
		fprintf(stderr, "\tnone      - space is closed, nobody must be inside\n");
		fprintf(stderr, "\tkeyholder - space is closed, keyholder is inside\n");
		fprintf(stderr, "\tmember    - space is open, but only for members\n");
		fprintf(stderr, "\topen      - space is open, guests may ring the bell\n");
		fprintf(stderr, "\topen+     - space is open, everyone can open the door\n");
		free(arg);
		return false;
	}
	free(arg);

	/* optional message */
	*msg = strdup(tmp ? (tmp + 1) : "");

	return true;
}

enum door {
	DOOR_INVALID = 0,
	DOOR_MAIN,
	DOOR_GLASS,
	DOOR_MAX
};

static const char* doors[] = {
	"invalid",
	"main",
	"glass"
};

enum door str2door(const char *door) {
	for(unsigned int i=0; i < ARRAYSIZE(doors); i++) {
		if(!strcmp(door, doors[i]))
			return i;
	}

	return DOOR_INVALID;
}


static bool parse_open_door_cmd(char *command, enum door *door) {
	*door = str2door(command);

	if (*door == DOOR_INVALID) {
		fprintf(stderr, "invalid door: %s\n", command);
		fprintf(stderr, "Possible door values:\n");
		fprintf(stderr, "\tmain  - main door to space\n");
		fprintf(stderr, "\tglass - glass door to corridor\n");
		return false;
	}

	return true;
}

static bool db_init(sqlite3 **db) {
	int err;
	char *err_msg = NULL;

	char *dbfile = cfg_get_default(cfg, "database", strdup(DATABASE));
	err = sqlite3_open(dbfile, db);
	free(dbfile);
	if (err != SQLITE_OK) {
		fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(*db));
		sqlite3_close(*db);
		return false;
	}

	char *query =
		"BEGIN TRANSACTION;"
		"CREATE TABLE IF NOT EXISTS user (id INTEGER PRIMARY KEY NOT NULL, username TEXT NOT NULL, firstname TEXT, lastname TEXT, email TEXT, pw TEXT);"
		"CREATE TABLE IF NOT EXISTS key (fingerprint CHARACTER(48) PRIMARY KEY NOT NULL, userid INTEGER NOT NULL REFERENCES user, type TEXT, base64 TEXT NOT NULL, comment TEXT, last_login INTEGER NOT NULL DEFAULT 0);"
		"CREATE TABLE IF NOT EXISTS log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER NOT NULL, login_timestamp INTEGER NOT NULL, userid INTEGER NOT NULL REFERENCES user, ip TEXT, key CHARACTER(48) NOT NULL REFERENCES key, mode INTEGER NOT NULL, msg TEXT);"
		"COMMIT;";

	err = sqlite3_exec(*db, query, 0, 0, &err_msg);
	if (err != SQLITE_OK ) {
		fprintf(stderr, "SQL error: %s\n", err_msg);
		sqlite3_free(err_msg);
		sqlite3_close(*db);
		return false;
	}
	
	return true;
}

static bool db_get_uid(sqlite3 *db, char *username, int *userid) {
	int err;
	sqlite3_stmt *res;
	char *query;
	
	query = "SELECT id FROM user where username = ?";
	err = sqlite3_prepare_v2(db, query, -1, &res, 0);
	if (err != SQLITE_OK) {
		fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
		return false;
	}

	sqlite3_bind_text(res, 1, strdup(username), strlen(username), free);

	err = sqlite3_step(res);
	if (err == SQLITE_ROW) {
		*userid = sqlite3_column_int(res, 0);
		sqlite3_finalize(res);
		return true;
	}

	sqlite3_finalize(res);

	return false;
}

static bool db_update_key(sqlite3 *db, const char *fingerprint, int uid, const char *keytype, const char *base64, const char *comment, int last_login) {
	int err;
	sqlite3_stmt *res;
	char *query;
	
	query = "INSERT OR REPLACE INTO key VALUES (?, ?, ?, ?, ?, ?)";
	err = sqlite3_prepare_v2(db, query, -1, &res, 0);
	if (err != SQLITE_OK) {
		fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
		return false;
	}

	sqlite3_bind_text(res, 1, strdup(fingerprint), strlen(fingerprint), free);
	sqlite3_bind_int(res, 2, uid);
	sqlite3_bind_text(res, 3, strdup(keytype), strlen(keytype), free);
	sqlite3_bind_text(res, 4, strdup(base64), strlen(base64), free);
	sqlite3_bind_text(res, 5, strdup(comment), strlen(comment), free);
	sqlite3_bind_int(res, 6, last_login);

	err = sqlite3_step(res);
	sqlite3_finalize(res);

	if (err == SQLITE_DONE)
		return true;
	else
		return false;
}

static bool db_insert_log(sqlite3 *db, time_t login_time, int userid, const char *ip, const char *keyfp, int mode, const char *msg) {
	int err;
	sqlite3_stmt *res;
	char *query;
	time_t now = time(NULL);

	query = "INSERT INTO log (timestamp, login_timestamp, userid, ip, key, mode, msg) VALUES (?, ?, ?, ?, ?, ?, ?)";
	err = sqlite3_prepare_v2(db, query, -1, &res, 0);
	if (err != SQLITE_OK) {
		fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
		return false;
	}

	sqlite3_bind_int(res, 1, now);
	sqlite3_bind_int(res, 2, login_time);
	sqlite3_bind_int(res, 3, userid);
	sqlite3_bind_text(res, 4, strdup(ip), strlen(ip), free);
	sqlite3_bind_text(res, 5, strdup(keyfp), strlen(keyfp), free);
	sqlite3_bind_int(res, 6, mode);
	sqlite3_bind_text(res, 7, strdup(msg), strlen(msg), free);

	err = sqlite3_step(res);
	sqlite3_finalize(res);

	if (err == SQLITE_DONE)
		return true;
	else
		return false;
}

static bool write_file(const char *dir, const char *filename, const char *data) {
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

	if (written < len) {
		return false;
	} else {
		return true;
	}
}

int main(int argc, char **argv) {
	pid_t pid;
	time_t logintime;
	int keyuid;
	char *ip, *keytype, *keyfp, *keydata, *keycomment, *keyuser;
	sqlite3 *db = NULL;
	int mode = -1, next_mode = -1;
	char *msg = NULL;
	char *keyuidstr = NULL;
	enum door door;
	char *command = NULL;
	enum cmd cmd;

	cfg = cfg_open();

	char *statedir = cfg_get_default(cfg, "statedir", STATEDIR);

	if (argc == 1) {
		sd_journal_print(LOG_NOTICE, "providing pseudo shell");
		command = readline("acs> ");
	} else if (argc == 3 && !strcmp(argv[1], "-c")) {
		command = strdup(argv[2]);
	} else {
		fprintf(stderr, "invalid parameters\n");
		sd_journal_print(LOG_ERR, "invalid parameters");
		goto error;
	}

	cmd = get_command(&command);

	sd_journal_print(LOG_DEBUG, "keyholder-interface: cmd=%s arguments=%s", cmd_str[cmd], command);

	switch (cmd) {
		case CMD_SET_STATUS:
			if (!parse_status_cmd(command, &mode, &msg))
				goto error;
			break;
		case CMD_SET_NEXT_STATUS:
			if (!parse_status_cmd(command, &next_mode, &msg))
				goto error;
			break;
		case CMD_OPEN_DOOR:
			if (!parse_open_door_cmd(command, &door))
				goto error;
			break;
		case CMD_INVALID:
		default:
			fprintf(stderr, "Supported commands:\n");
			fprintf(stderr, " set-status <status> [msg]\n");
			fprintf(stderr, " set-next-status <status> [msg]\n");
			fprintf(stderr, " open-door <door>\n");
			goto error;
	}

	if (!db_init(&db))
		goto error;

	/* get parent sshd process id */
	if (!find_sshd_parent(&pid))
		goto error;

	/* get public key fingerprint from ssh authentication logfile */
	if (!log_get_fingerprint(pid, &logintime, &ip, &keytype, &keyfp))
		goto error;

	if (!authorized_keys_get(keyfp, &keydata, &keycomment))
		goto error;

	keyuser = keycomment2username(keycomment);

	if (!db_get_uid(db, keyuser, &keyuid)) {
		fprintf(stderr, "User '%s' not in database!\n", keyuser);
		goto error;
	}

	if (!db_update_key(db, keyfp, keyuid, keytype, keydata, keycomment, logintime)) {
		fprintf(stderr, "DB: Could not update key in key table!\n");
		goto error;
	}

	sd_journal_print(LOG_NOTICE, "keyholder-interface: Identified user %s (%d) with key %s", keyuser, keyuid, keyfp);

	/* current status is available from simple files */
	if (mkdir(statedir, mode) && errno != EEXIST) {
		fprintf(stderr, "Could not create statedir '%s'!\n", statedir);
		goto error;
	}

	if (cmd == CMD_SET_STATUS || cmd == CMD_SET_NEXT_STATUS) {
		if (!db_insert_log(db, logintime, keyuid, ip, keyfp, mode, msg)) {
			fprintf(stderr, "DB: Could not insert into log table!\n");
			goto error;
		}

		if (asprintf(&keyuidstr, "%d", keyuid) < 0) {
			fprintf(stderr, "asprintf failed!\n");
			goto error;
		}
		write_file(statedir, "keyholder-id", keyuidstr);
		free(keyuidstr);
		write_file(statedir, "keyholder-name", keyuser);
		if (mode >= 0)
			write_file(statedir, "status", modes[mode]);
		if (next_mode == -1)
			write_file(statedir, "status-next", "");
		else
			write_file(statedir, "status-next", modes[next_mode]);
		write_file(statedir, "message", msg);
		free(statedir);

		printf("Keyholder:   %s (%d)\n", keyuser, keyuid);
		if (mode >= 0) {
			sd_journal_print(LOG_NOTICE, "set status %s", modes[mode]);
			printf("Status:      %s (%d)\n", modes[mode], mode);
		} if (next_mode >= 0) {
			printf("Next-Status: %s (%d)\n", modes[next_mode], next_mode);
			sd_journal_print(LOG_NOTICE, "set next-status %s", modes[next_mode]);
		}
		printf("Message:     %s\n", msg);
	} else if(cmd == CMD_OPEN_DOOR) {
		write_file(statedir, "open-door", doors[door]);
		sd_journal_print(LOG_NOTICE, "keyholder-interface: open-door %s", doors[door]);
		printf("open door: %s\n", doors[door]);
	}

	sqlite3_close(db);
	cfg_close(cfg);
	free(msg);

	return 0;

error:
	sqlite3_close(db);
	cfg_close(cfg);
	free(msg);
	return 1;
}
