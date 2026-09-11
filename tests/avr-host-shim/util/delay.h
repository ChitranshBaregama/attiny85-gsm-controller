#ifndef HOST_UTIL_DELAY_H
#define HOST_UTIL_DELAY_H
/* Delays do nothing on the host. Anything whose correctness depends on a
 * real delay is by definition not testable here, and this harness does
 * not pretend otherwise. */
static inline void _delay_us(double us) { (void)us; }
static inline void _delay_ms(double ms) { (void)ms; }
#endif
