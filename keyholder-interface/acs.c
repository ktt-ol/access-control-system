/*
 * Access Control System
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

#define _GNU_SOURCE /* asprintf is specific to GNU and *BSD based systems */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pcre.h>
#include <time.h>
#include <sqlite3.h>
#include <openssl/evp.h>

#include "../common/config.h"

#define SSHDNAME "sshd"

#ifndef bool
#define bool char
#endif
#ifndef false
#define false 0
#endif
#ifndef true
#define true 1
#endif

static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static const char* modes[] = { "unknown", "none", "keyholder", "member", "public", "open" };

/* (Month) (Day) (Hour):(Minute):(Second) (Hostname) (Processname)[(Processid)]: (Message) */
//static const char *LOG_REGEX = "^([a-z]{3}) ([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2}) ([^ ]+) ([a-zA-Z]+)\\[([0-9]+)\\]: (.*)$";
static const char *LOG_REGEX = "^([A-Z][a-z]{2}) ([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2}) ([^ ]+) ([a-zA-Z]+)\\[([0-9]+)\\]: (.*)$";

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
			*pid = process_get_parent(p);
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
	EVP_MD_CTX mdctx;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	unsigned int md_len;
	int err;
	char *result;

	key_raw_len = EVP_DecodeBlock(key_raw, (unsigned char *) key_base64, key_base64_len); 

	EVP_MD_CTX_init(&mdctx);
	EVP_DigestInit_ex(&mdctx, EVP_md5(), NULL);

	err = EVP_DigestUpdate(&mdctx, key_raw, key_raw_len);
	if (err != 1)
		return NULL;

	err = EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
	if (err != 1)
		return NULL;
	
	EVP_MD_CTX_cleanup(&mdctx);

	result = malloc(16*3);
	if (!result)
		return NULL;
	
	snprintf(result, 16*3, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		md_value[0], md_value[1], md_value[2], md_value[3], md_value[4], md_value[5], md_value[6], md_value[7],
		md_value[8], md_value[9], md_value[10], md_value[11], md_value[12], md_value[13], md_value[14], md_value[15]);
	
	return result;
}

static bool authorized_keys_get(char *keytype, char *keyfp, char **key, char **comment) {
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	size_t keytypelen = strlen(keytype);

	char *kd, *kc;

	char *keyfile = cfg_get_default(cfg, "ssh-keyfile", strdup(SSHKEYFILE));
	f = fopen(keyfile, "r");
	free(keyfile);
	if (!f)
		return false;

	// Format: "ssh-(type) (base64'd pubkey) (comment)"
	while ((read = getline(&line, &len, f)) != -1) {
		if (read < 4) {
			fprintf(stderr, "Malformed authorized_keys file!\n");
			break;
		}

		if (strncasecmp(keytype, line+4, keytypelen)) {
			/* keytype mismatch */
			continue;
		}

		kd = line + 4 + keytypelen + 1; 
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

unsigned int str2mode(char *mode) {
	for(unsigned int i=0; i < sizeof(modes); i++) {
		if(!strcmp(mode, modes[i]))
			return i;
	}

	return 0;
}

static bool parse_arguments(int argc, char **argv, unsigned int *mode, char **msg) {
	int len;

	/* supplying mode is mandatory */
	if (argc < 2)
		goto error;
	
	/* supplied mode does not exist */
	*mode = str2mode(argv[1]);
	if (*mode <= 0 || *mode >= sizeof(modes))
		goto error;

	if (argc < 3) {
		*msg = strdup("");
		return true;
	}

	*msg = strdup(argv[2]);
	len = strlen(*msg);

	for (int i=3; i < argc; i++) {
		int arglen = strlen(argv[i]);
		if (!realloc(*msg, len + arglen + 1)) {
			free(*msg);
			fprintf(stderr, "Out of memory!\n");
			return false;
		}
		(*msg)[len] = ' ';
		memcpy(*msg+len+1, argv[i], arglen);
		(*msg)[len+arglen+1] = '\0';
		len += arglen + 1;
	}

	return true;

error:
	fprintf(stderr, "Usage: %s <mode> [msg...]\n\n", argv[0]);
	fprintf(stderr, "Possible modes:\n");
	fprintf(stderr, "\tnone      - space is closed, nobody must be inside\n");
	fprintf(stderr, "\tkeyholder - space is closed, keyholder is inside\n");
	fprintf(stderr, "\tmember    - space is open, but only for members\n");
	fprintf(stderr, "\tpublic    - space is open, guests may ring the bell\n");
	fprintf(stderr, "\topen      - space is open, everyone can open the door\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "This command will set the space status to the described mode.\n");
	fprintf(stderr, "An optional human readable message can be supplied by the keyholder.\n");
	return false;
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

	f = fopen(filename, "w");
	if (!f)
		return false;
	
	written = fwrite(data, 1, len, f);
	fwrite("\n", 1, 1, f);
	fclose(f);

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
	sqlite3 *db;
	unsigned int mode;
	char *msg = NULL;
	char *keyuidstr = NULL;

	cfg = cfg_open();

	if (!parse_arguments(argc, argv, &mode, &msg))
		goto error;

	if (!db_init(&db))
		goto error;

	/* get parent sshd process id */
	if (!find_sshd_parent(&pid))
		goto error;

	/* get public key fingerprint from ssh authentication logfile */
	if (!log_get_fingerprint(pid, &logintime, &ip, &keytype, &keyfp))
		goto error;

	if (!authorized_keys_get(keytype, keyfp, &keydata, &keycomment))
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

	if (!db_insert_log(db, logintime, keyuid, ip, keyfp, mode, msg)) {
		fprintf(stderr, "DB: Could not insert into log table!\n");
		goto error;
	}

	char *statedir = cfg_get_default(cfg, "statedir", STATEDIR);
	/* current status is available from simple files */
	if (mkdir(statedir, mode) && errno != -EEXIST) {
		fprintf(stderr, "Could not create statedir '%s'!\n", statedir);
		goto error;
	}

	if (asprintf(&keyuidstr, "%d", keyuid) < 0) {
		fprintf(stderr, "asprintf failed!\n");
		goto error;
	}
	write_file(statedir, "keyholder-id", keyuidstr);
	free(keyuidstr);
	write_file(statedir, "keyholder-name", keyuser);
	write_file(statedir, "status", modes[mode]);
	write_file(statedir, "message", msg);
	free(statedir);

	printf("SSH Key %s accepted!\n\n", keyfp);
	printf("Keyholder: %s (%d)\n", keyuser, keyuid);
	printf("Status:    %s (%d)\n", modes[mode], mode);
	printf("Message:   %s\n", msg);

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
