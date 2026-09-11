#include <stdint.h>
uint8_t DDRB, PORTB, PINB, PCMSK, GIFR, GIMSK, OSCCAL = 0x80, SREG;
uint8_t TCCR0A, TCCR0B, TIMSK;
uint8_t host_eeprom[512];
unsigned long host_millis = 0;
