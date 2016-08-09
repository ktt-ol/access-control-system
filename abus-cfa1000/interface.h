#ifndef __ABUS_CFA1000_INTERFACE_H
#define __ABUS_CFA1000_INTERFACE_H

enum lock_state {
	LOCK_STATE_LOCKED,
	LOCK_STATE_UNLOCKED,
	LOCK_STATE_UNKNOWN
};

struct display_data_t {
	char symbol;
	enum lock_state state;
};

struct display_data_t display_read(int i2cdev);

static char* lock_state_str(enum lock_state state) {
	switch(state) {
		case LOCK_STATE_LOCKED:
			return "locked";
		case LOCK_STATE_UNLOCKED:
			return "unlocked";
		default:
			return "unknown";
	}
}

#endif
