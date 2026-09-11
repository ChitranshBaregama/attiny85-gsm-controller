/* Host stand-ins for the ATtiny85 registers.
 *
 * Ordinary variables. The point is to compile the REAL sketch on a
 * development machine so its protocol logic can be exercised - the line
 * parser, the call state machine, the command de-duplication - none of
 * which need silicon to be wrong.
 *
 * Testable here: everything that is a pure function of the bytes arriving
 * from the modem. Not testable here: bit-banged UART timing, OSCCAL
 * calibration, anything analogue. Those need the part.
 */
#ifndef HOST_AVR_IO_H
#define HOST_AVR_IO_H
#include <stdint.h>
extern uint8_t DDRB, PORTB, PINB, PCMSK, GIFR, GIMSK, OSCCAL, SREG;
extern uint8_t TCCR0A, TCCR0B, TIMSK;
#define PB0 0
#define PB1 1
#define PB2 2
#define PB3 3
#define PB4 4
#define PB5 5
#define PCIF  5
#define PCIE  5
#define TOIE0 1
#define CS00  0
#define CS01  1
#endif
