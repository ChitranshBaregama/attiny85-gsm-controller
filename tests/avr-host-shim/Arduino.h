#ifndef HOST_ARDUINO_H
#define HOST_ARDUINO_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
/* A settable clock, so tests can jump forward past a de-duplication window
 * or a stale-ring timeout without actually waiting 15 seconds. */
extern unsigned long host_millis;
static inline unsigned long millis(void) { return host_millis; }
#endif
