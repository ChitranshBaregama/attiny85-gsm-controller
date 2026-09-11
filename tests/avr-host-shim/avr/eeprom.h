#ifndef HOST_AVR_EEPROM_H
#define HOST_AVR_EEPROM_H
#include <stdint.h>
extern uint8_t host_eeprom[512];
static inline uint8_t eeprom_read_byte(const uint8_t *addr)
{ return host_eeprom[(uintptr_t)addr & 511u]; }
static inline void eeprom_write_byte(uint8_t *addr, uint8_t v)
{ host_eeprom[(uintptr_t)addr & 511u] = v; }
#endif
