#include <avr/io.h>
#include <util/delay.h>
#include "light_ws2812.h"
#include "usiTwiSlave.h"

struct cRGB leds[LED_COUNT];

void i2c_recv(uint8_t reg, uint8_t val) {
	uint8_t ledid = reg / 3;
	uint8_t color = reg % 3;

	if (ledid >= LED_COUNT)
		return;

	txbuffer[reg] = val;

	switch(color) {
		case 0:
			leds[ledid].r = val;
			break;
		case 1:
			leds[ledid].g = val;
			break;
		case 2:
			leds[ledid].b = val;
			break;
	}
}

void init_leds() {
	int i;
	for(i=0; i < LED_COUNT; i++) {
		i2c_recv(i*3+0, 5);
		i2c_recv(i*3+1, 5);
		i2c_recv(i*3+2, 5);
	}
}

void main(void)
{
	init_leds();
	ws2812_setleds(leds, LED_COUNT);

	cli();
	usiTwiSlaveInit(I2C_ADDR);
	sei();

	while(1) {
		if (!i2c_changes)
			_delay_ms(500);
		i2c_changes = 0;

		ws2812_setleds(leds, LED_COUNT);
	}
}
