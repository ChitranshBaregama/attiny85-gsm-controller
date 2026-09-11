# Bare-metal build of the sketch: no Arduino installation required.
#
#   make          compile and report flash / RAM usage
#   make hex      produce the .hex for avrdude
#   make fuses    print the fuse settings this design needs
#   make clean
#
# The .ino is compiled unmodified. compat/ supplies millis() and main();
# everything else the sketch needs comes from avr-libc.

MCU    := attiny85
F_CPU  := 8000000UL
TARGET := gsm-controller
SKETCH := ANANT_IONS_SIM900A_ATTINY85_V11.ino

CC      := avr-gcc
OBJCOPY := avr-objcopy
SIZE    := avr-size

# -x c++ because the sketch is C++ (Arduino dialect) and the .ino
# extension tells the compiler nothing.
CXXFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=gnu++11 \
            -Wall -Wextra \
            -ffunction-sections -fdata-sections \
            -fno-exceptions -fno-threadsafe-statics \
            -I../compat
CFLAGS   := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=gnu11 -Wall -Wextra \
            -ffunction-sections -fdata-sections -I../compat
LDFLAGS  := -mmcu=$(MCU) -Wl,--gc-sections -Wl,-Map=$(TARGET).map

.PHONY: all hex fuses clean

all: $(TARGET).elf
	@echo
	@$(SIZE) --format=avr --mcu=$(MCU) $(TARGET).elf

$(TARGET).elf: sketch.o millis.o main.o
	$(CC) $(LDFLAGS) $^ -o $@

sketch.o: $(SKETCH)
	$(CC) -x c++ $(CXXFLAGS) -c $< -o $@

millis.o: ../compat/millis.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compiled as C++ so setup()/loop() resolve against the sketch's symbols.
main.o: ../compat/main.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $(TARGET).hex
	@ls -l $(TARGET).hex

fuses:
	@echo "ATtiny85, 8 MHz internal RC, no divide-by-8:"
	@echo "  lfuse 0xE2   hfuse 0xDF   efuse 0xFF"
	@echo
	@echo "  avrdude -c usbasp -p t85 -U lfuse:w:0xE2:m -U hfuse:w:0xDF:m"
	@echo
	@echo "CKDIV8 must be OFF. With it on the part runs at 1 MHz, every"
	@echo "bit-banged delay is 8x too long, and nothing decodes."

clean:
	rm -f *.o *.elf *.hex *.map
