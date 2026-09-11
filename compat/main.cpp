/* Entry point for the bare-metal build. The Arduino IDE supplies its own
 * main() that does the same thing.
 *
 * This is C++, not C, on purpose: the sketch is compiled as C++, so its
 * setup() and loop() carry mangled C++ symbol names. A C main() looks for
 * unmangled ones and the link fails with "undefined reference to setup". */
#include "Arduino.h"

void setup(void);
void loop(void);

int main(void)
{
    millis_init();
    setup();
    for (;;) {
        loop();
    }
}
