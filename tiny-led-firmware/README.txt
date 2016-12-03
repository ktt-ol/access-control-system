=== Mode ===

The device is either in LED or in I2C mode. The mode
is selected via PB3. If PB3 is low, the device is
in LED mode and updates the ws2812b LEDs with an 32
Hz interval. If PB3 is high, the device is in I2C
mode and listens to its device address. Due to HW
limitations the device does not listen to its i2c
address while its in the LED mode.

=== I2C Commands ===

send:    [DEV-ADDR] [LED-ADDR] [R] [G] [B] [C]
receive: [DEV-ADDR] [LED-ADDR] => [R] [G] [B] [C]

=> behaves like a 8-bit address, 32-bit value i2c-eeprom
=> auto-address-increment is supported, so you can use multi-read/write

=== Byte Description ===

[DEV-ADDR]
The address of the I2C slave device. Configured
in config.h, by default 0x23.

[LED-ADDR]
The ID of the LED inside of the WS2812b stripe.
This is modulo the number of configured LEDs in
config.h (by default 50).

[R] [G] [B]
Color values for red, green and blue.

[C]
This is a configuration byte for the LED mode.
Bits 7 & 8 describe the mode:

00 = set directly
01 = fade
10 = blink (on/off)
11 = glow (decrease brightness by 25% and come back)

The remaining bits are a time value, which is multiplied
by 31.25ms (32Hz base). For fade mode it describes how
long it the (linear) fading should take. For blink and
glow mode it describes how long a half period
(dark -> bright / bright -> dark) should take.
