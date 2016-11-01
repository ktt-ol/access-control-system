#ifndef __CONFIG_H
#define __CONFIG_H

/* must be an integer between 1 and 255, limited by max memory size of AVR */
#define LED_COUNT 50

/* shift address to follow the standard format */
#define I2C_ADDR  0x23 << 1

/* pin used for ws2812b stripe */
#define WS2812_PORT B
#define WS2812_PIN  1

#endif
