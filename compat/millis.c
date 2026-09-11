#include "Arduino.h"

/* Timer0 overflow counter. 8 MHz / 64 = 125 kHz, so one overflow is
 * 256 / 125000 = 2.048 ms. */
static volatile unsigned long g_overflows = 0;

ISR(TIM0_OVF_vect)
{
    g_overflows++;
}

void millis_init(void)
{
    TCCR0A = 0;
    TCCR0B = (1 << CS01) | (1 << CS00);     /* prescaler /64 */
    TIMSK |= (1 << TOIE0);
}

unsigned long millis(void)
{
    unsigned long v;
    uint8_t sreg = SREG;
    cli();
    v = g_overflows;
    SREG = sreg;

    /* v * 2.048 ms, in integer arithmetic: v * 2048 / 1000 */
    return (v * 2048UL) / 1000UL;
}
