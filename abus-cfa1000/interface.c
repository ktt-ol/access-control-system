#include <stdint.h>
#include "../common/i2c.h"
#include "interface.h"

#define MCP23017_REG_GPIO_A 0x12
#define MCP23017_REG_GPIO_B 0x13

#define MCP23017_GPIO_A0 0x0001
#define MCP23017_GPIO_A1 0x0002
#define MCP23017_GPIO_A2 0x0004
#define MCP23017_GPIO_A3 0x0008
#define MCP23017_GPIO_A4 0x0010
#define MCP23017_GPIO_A5 0x0020
#define MCP23017_GPIO_A6 0x0040
#define MCP23017_GPIO_A7 0x0080
#define MCP23017_GPIO_B0 0x0100
#define MCP23017_GPIO_B1 0x0200
#define MCP23017_GPIO_B2 0x0400
#define MCP23017_GPIO_B3 0x0800
#define MCP23017_GPIO_B4 0x1000
#define MCP23017_GPIO_B5 0x2000
#define MCP23017_GPIO_B6 0x4000
#define MCP23017_GPIO_B7 0x8000

#define DISP_VERT_BOT_L		MCP23017_GPIO_A1
#define DISP_VERT_BOT_M		MCP23017_GPIO_A3
#define DISP_VERT_BOT_R		MCP23017_GPIO_A2
#define DISP_VERT_TOP_L		MCP23017_GPIO_B5
#define DISP_VERT_TOP_M		MCP23017_GPIO_B7
#define DISP_VERT_TOP_R		MCP23017_GPIO_B2
#define DISP_HORI_TOP		MCP23017_GPIO_B1
#define DISP_HORI_BOT		MCP23017_GPIO_A5
#define DISP_HORI_MID_L		MCP23017_GPIO_B6
#define DISP_HORI_MID_R		MCP23017_GPIO_A0
#define DISP_DIAG_TOP_L		MCP23017_GPIO_B4
#define DISP_DIAG_TOP_R		MCP23017_GPIO_B3
#define DISP_DIAG_BOT_L		MCP23017_GPIO_A4
#define DISP_DIAG_BOT_R		MCP23017_GPIO_A7
#define DISP_LOCKED			MCP23017_GPIO_A6
#define DISP_UNLOCKED		MCP23017_GPIO_B0

#define DISPLAY_1			DISP_VERT_TOP_R | DISP_VERT_BOT_R
#define DISPLAY_2			DISP_HORI_TOP | DISP_VERT_TOP_R | DISP_HORI_MID_L | DISP_HORI_MID_R | DISP_VERT_BOT_L | DISP_HORI_BOT
#define DISPLAY_3			DISP_HORI_TOP | DISP_VERT_TOP_R | DISP_HORI_MID_L | DISP_HORI_MID_R | DISP_VERT_BOT_R | DISP_HORI_BOT
#define DISPLAY_4			DISP_VERT_TOP_R | DISP_VERT_BOT_R | DISP_VERT_TOP_L | DISP_HORI_MID_L | DISP_HORI_MID_R
#define DISPLAY_SLASH		DISP_DIAG_BOT_L | DISP_DIAG_TOP_R
#define DISPLAY_BACKSLASH	DISP_DIAG_TOP_L | DISP_DIAG_BOT_R
#define DISPLAY_MINUS		DISP_HORI_MID_L | DISP_HORI_MID_R
#define DISPLAY_PIPE		DISP_VERT_TOP_M | DISP_VERT_BOT_M
#define DISPLAY_M			DISP_VERT_BOT_L | DISP_VERT_TOP_L | DISP_DIAG_TOP_L | DISP_DIAG_TOP_R | DISP_VERT_BOT_R | DISP_VERT_TOP_R

static struct display_data_t display_decode(uint16_t val) {
	struct display_data_t data;

	if ((val & DISP_LOCKED) && (val & DISP_UNLOCKED))
		data.state = LOCK_STATE_UNKNOWN;
	else if (val & DISP_LOCKED)
		data.state = LOCK_STATE_LOCKED;
	else if (val & DISP_UNLOCKED)
		data.state = LOCK_STATE_UNLOCKED;
	else
		data.state = LOCK_STATE_UNKNOWN;

	val &= ~(DISP_LOCKED | DISP_UNLOCKED);
	switch(val) {
		case DISPLAY_M:
			data.symbol = 'M';
			break;
		case DISPLAY_1:
			data.symbol = '1';
			break;
		case DISPLAY_2:
			data.symbol = '2';
			break;
		case DISPLAY_3:
			data.symbol = '3';
			break;
		case DISPLAY_4:
			data.symbol = '4';
			break;
		case DISPLAY_SLASH:
			data.symbol = '/';
			break;
		case DISPLAY_BACKSLASH:
			data.symbol = '\\';
			break;
		case DISPLAY_MINUS:
			data.symbol = '-';
			break;
		case DISPLAY_PIPE:
			data.symbol = '|';
			break;
		default:
			data.symbol = '\0';
			break;
	}

	return data;
}

struct display_data_t display_read(int i2cdev) {
	uint16_t val = i2c_read16(i2cdev, MCP23017_REG_GPIO_A);
	return display_decode(val);
}
