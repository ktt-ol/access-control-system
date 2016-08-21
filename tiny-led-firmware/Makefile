attiny-sw: main.c light_ws2812.c usiTwiSlave.c
	avr-gcc -mmcu=attiny85 -Os -DF_CPU=8000000 -ffunction-sections -fdata-sections -o $@ $^

main.c: config.h light_ws2812.h usiTwiSlave.h ws2812_config.h
	@touch main.c

flash: attiny-sw
	avrdude -c buspirate -P /dev/ttyUSB0 -p t85 -U flash:w:$<

fuses:
	avrdude -c buspirate -P /dev/ttyUSB0 -p attiny85 -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U efuse:w:0xff:m

clean:
	rm -f attiny-sw main.o light_ws2812.o usiTwiSlave.o

.PHONY: flash fuses clean
