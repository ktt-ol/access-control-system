#ifndef __STATE_H
#define __STATE_H

#include <stdbool.h>
#include <string.h>

enum state {
	STATE_UNKNOWN = 0,
	STATE_NONE,
	STATE_KEYHOLDER,
	STATE_MEMBER,
	STATE_PUBLIC,
	STATE_OPEN,
	STATE_MAX
};

extern const char* states[];

enum lock_state {
	LOCK_STATE_UNKNOWN,
	LOCK_STATE_UNLOCKED,
	LOCK_STATE_LOCKED,
	LOCK_STATE_ERROR
};

static inline enum state str2state(const char *mode) {
	if (!mode)
		return 0;

	for(unsigned int i=0; i < STATE_MAX; i++) {
		if(!strcmp(mode, states[i]))
			return i;
	}

	return 0;
}

static inline const char* state2str(enum state state) {
	if (state < 0 || state >= STATE_MAX)
		return states[STATE_UNKNOWN];
	else
		return states[state];
}

bool state_read(const char *statedir, int *keyholder_id, char **keyholder_name, enum state *status, char **message);
bool state_write(const char *statedir, int keyholder_id, const char *keyholder_name, enum state status, const char *message);

#endif
