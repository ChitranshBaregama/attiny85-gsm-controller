#ifndef HOST_AVR_INTERRUPT_H
#define HOST_AVR_INTERRUPT_H
/* Interrupts are meaningless on the host; the harness calls handlers
 * directly. ISR() becomes an ordinary function so the body still gets
 * compiled - code that is not compiled is code that is not reviewed. */
#define ISR(vector) void vector##_handler(void)
#define PCINT0_vect pcint0
#define TIM0_OVF_vect tim0_ovf
static inline void cli(void) { }
static inline void sei(void) { }
#endif
