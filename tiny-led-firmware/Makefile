attiny-sw: main.c ws2812.c i2c.c
	avr-gcc -mmcu=attiny85 -Os -DF_CPU=8000000 -ffunction-sections -fdata-sections -o $@ $^

ws2812.c: ws2812.h config.h
	@touch $@

i2c.c: i2c.h config.h
	@touch $@

main.c: ws2812.h i2c.h
	@touch $@

flash: attiny-sw
	avrdude -c buspirate -P /dev/ttyUSB0 -p t85 -U flash:w:$<

fuses:
	avrdude -c buspirate -P /dev/ttyUSB0 -p attiny85 -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U efuse:w:0xff:m

check:
	avr-size --format=avr --mcu=attiny85 attiny-sw

clean:
	rm -f attiny-sw main.o ws2812.o i2c.o

.PHONY: flash fuses clean check
