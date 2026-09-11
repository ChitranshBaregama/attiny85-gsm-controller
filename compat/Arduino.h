/* Minimal Arduino shim, so the sketch compiles with plain avr-gcc.
 *
 * The sketch is written against almost nothing from the Arduino core: it
 * talks to PORTB directly, bit-bangs its own UART, and uses PROGMEM and
 * _delay_us() from avr-libc. The only core facility it actually needs is
 * millis().
 *
 * Providing that here means:
 *   - CI can compile the firmware with no Arduino installation at all
 *   - the SAME .ino file builds in the IDE and from this Makefile, so the
 *     two cannot drift apart
 *   - the bare-metal image drops the Arduino core entirely, which on an
 *     8 KB part is worth measuring (see docs/BUILD.md)
 *
 * Timer0 on the ATtiny85 at 8 MHz with a /64 prescaler overflows every
 * 256 * 64 / 8e6 = 2.048 ms. Counting overflows and scaling gives a
 * millisecond clock accurate to about 2.4%, which is the same order as
 * the internal RC oscillator this design already calibrates against the
 * modem - so it is not the limiting error.
 */
#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void     millis_init(void);
unsigned long millis(void);

#ifdef __cplusplus
}
#endif

#endif
