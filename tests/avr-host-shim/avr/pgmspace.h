#ifndef HOST_AVR_PGMSPACE_H
#define HOST_AVR_PGMSPACE_H
/* On AVR, PROGMEM keeps constants in flash and pgm_read_byte fetches them
 * with LPM. On the host they are just bytes in .rodata. */
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif
