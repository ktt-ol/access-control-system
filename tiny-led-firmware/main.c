#include <avr/io.h>
#include <util/delay.h>
#include <stdbool.h>
#include "ws2812.h"
#include "i2c.h"

struct LEDcmd { uint8_t g; uint8_t r; uint8_t b; uint8_t c; uint8_t h;};

static struct cRGB leds[LED_COUNT];
static struct LEDcmd cmds[LED_COUNT];
volatile static bool update = false;
volatile static bool stopped = false;

#define LED_CMD_MASK	0xc0
#define LED_CMD_SHIFT	6
#define LED_CMD_SET		0x0
#define LED_CMD_FADE	0x1
#define LED_CMD_BLINK	0x2
#define LED_CMD_GLOW	0x3

#define LED_TIME_MASK	0x3f

static void led_worker();

void i2c_recv(uint8_t reg, struct i2c_data val) {
	if (reg == 0xff){
		if (val.data0 || val.data1 || val.data2 || val.data3)
			stopped = true;
		else
			stopped = false;
		return;
	}

	if (reg >= LED_COUNT)
		return;

	cmds[reg].r = val.data0;
	cmds[reg].g = val.data1;
	cmds[reg].b = val.data2;
	cmds[reg].c = val.data3;
	cmds[reg].h = 0x00;
}

void i2c_send(uint8_t reg, struct i2c_data *val) {
	if (reg == 0xff){
		uint8_t result = stopped ? 0xff : 0x00;
		val->data0 = result;
		val->data1 = result;
		val->data2 = result;
		val->data3 = result;
		return;
	}

	if (reg >= LED_COUNT)
		return;

	val->data0 = cmds[reg].r;
	val->data1 = cmds[reg].g;
	val->data2 = cmds[reg].b;
	val->data3 = cmds[reg].c;
}

static void leds_init() {
	struct i2c_data init = {5,5,5,0};
	int i;

	for(i=0; i < LED_COUNT; i++)
		i2c_recv(i, init);

	led_worker();

	ws2812_setleds(leds, LED_COUNT);
}

static void fade_step_color(uint8_t *old, uint8_t new, uint8_t steps) {
	bool mode;
	uint8_t diff;
	uint8_t change;

	if (steps <= 1) {
		*old = new;
		return;
	}

	if (*old > new) {
		diff = *old - new;
		mode = false;
	} else {
		diff = new - *old;
		mode = true;
	}

	change = diff / steps;

	if (mode)
		*old += change;
	else
		*old -= change;
}

static void fade_step_mode(uint8_t steps, uint8_t *cfg) {
	steps--;

	if (steps) {
		/* decrease number of available steps */
		*cfg = (LED_CMD_FADE << LED_CMD_SHIFT) | steps;
	} else {
		/* fading done, switch to set mode */
		*cfg = 0x00;
	}
}

static void blink_step_color(uint8_t *old, uint8_t base, uint8_t cur) {
	if (cur)
		return;

	if (*old)
		*old = 0x00;
	else
		*old = base;
}

static void blink_step_mode(uint8_t steps, uint8_t *cur) {
	(*cur)++;
	if (*cur >= steps)
		*cur = 0x00;
}

static void glow_step_color(uint8_t *old, uint8_t base, uint8_t steps, uint8_t cur) {
	uint8_t curstep = cur & LED_TIME_MASK;

	/* calculate new color value */
	uint16_t val = base >> 2; /* 25% */
	val *= curstep;
	val /= steps;
	val %= 256;

	val = base - val;

	if (curstep == 0)
		*old = base;
	else if (curstep == steps)
		*old = base;
	else
		*old = val;

}

static void glow_step_mode(uint8_t steps, uint8_t *cur) {
	bool mode = (*cur) & 0x80;
	uint8_t curstep = (*cur) & LED_TIME_MASK;

	if (mode) {
		if (curstep == 0)
			mode = false;
		else
			curstep--;
	} else {
		if (curstep == steps)
			mode = true;
		else
			curstep++;
	}

	*cur = (mode << 7) | curstep;
}

static void led_worker() {
	int i;
	for (i=0; i < LED_COUNT; i++) {
		uint8_t t = cmds[i].c & LED_TIME_MASK;
		uint8_t c = (cmds[i].c & LED_CMD_MASK) >> LED_CMD_SHIFT;
		switch(c) {
			case LED_CMD_SET:
				leds[i].r = cmds[i].r;
				leds[i].g = cmds[i].g;
				leds[i].b = cmds[i].b;
				break;
			case LED_CMD_FADE:
				fade_step_color(&leds[i].r, cmds[i].r, t);
				fade_step_color(&leds[i].g, cmds[i].g, t);
				fade_step_color(&leds[i].b, cmds[i].b, t);
				fade_step_mode(t, &cmds[i].c);
				break;
			case LED_CMD_BLINK:
				blink_step_color(&leds[i].r, cmds[i].r, cmds[i].h);
				blink_step_color(&leds[i].g, cmds[i].g, cmds[i].h);
				blink_step_color(&leds[i].b, cmds[i].b, cmds[i].h);
				blink_step_mode(t, &cmds[i].h);
				break;
			case LED_CMD_GLOW:
				glow_step_color(&leds[i].r, cmds[i].r, t, cmds[i].h);
				glow_step_color(&leds[i].g, cmds[i].g, t, cmds[i].h);
				glow_step_color(&leds[i].b, cmds[i].b, t, cmds[i].h);
				glow_step_mode(t, &cmds[i].h);
				break;
		}
	}

	if(i2c_active())
		return;

	ws2812_setleds(leds, LED_COUNT);
}

static void timer_init() {
	cli();
	TCCR1 |= 0x80; /* ctc mode */
	TCCR1 |= 0xb; /* div1024 */
	OCR1C = F_CPU/1024 * 0.03125 - 1;	/* ca. 32Hz */
	TIMSK |= (1<<OCIE1A);				/* enable IRQ */
	sei();

	/* 32Hz = 31.25ms = 250000 instructions */
	/* 32Hz means, LED cfg (2**6) can count up to 2 seconds */
	/* 50 ws2812b LEDs: 50*3*1.25us + 10us = ~0.2ms */
	/* so we have > 30ms to calculate LED colors */
}

ISR(TIMER1_COMPA_vect) {
	update = true;
}

void main(void)
{
	leds_init();
	i2c_init(I2C_ADDR);
	timer_init();

	while(1) {
		if (update && !stopped) {
			led_worker();
			update = false;
		}
	}
}
