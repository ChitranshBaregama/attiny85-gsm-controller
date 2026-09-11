# Design Notes and Known Limitations

> What the firmware does on purpose, what it gets right that is easy to get
> wrong, and what it does not do. Written so that the next person to open
> this — including you in a year — does not have to rediscover any of it.

**Contents**
[Why bit-bang the UART](#why-bit-bang-the-uart) ·
[OSCCAL calibration](#osccal-calibration) ·
[Why CVHU=0 matters](#why-cvhu0-matters) ·
[Flags, never transmit, in the parser](#flags-never-transmit-in-the-parser) ·
[Security](#security-caller-id-is-not-authentication) ·
[Known limitations](#known-limitations) ·
[Resource budget](#resource-budget)

---

## Why bit-bang the UART

The ATtiny85 has no hardware USART. It has USI, which can be coerced into
serial, but USI needs a timer and careful clocking, and this design needs the
timer elsewhere.

So the UART is software, on two pins:

- **TX** is a delay loop with interrupts disabled for the duration of a byte,
  so no interrupt can stretch a bit.
- **RX** is a pin-change interrupt on the start bit, then sampling at the
  middle of each bit.

The timing constants carry deliberate corrections:

```c
#define BIT_US        104.17          /* 9600 baud                      */
#define TX_BIT_US     (BIT_US - 1.6)  /* minus the loop's own overhead  */
#define RX_BIT_US     (BIT_US - 2.4)
#define RX_START_US   (BIT_US * 1.5 - 6.0)   /* 1.5 bits to mid-bit 0,
                                                minus ISR entry latency */
```

Those subtractions are not fudge factors. The instructions that test the pin
and loop take real cycles, and at 8 MHz a bit is only 833 cycles — an
uncorrected loop drifts far enough by bit 8 to sample the stop bit as data.
The `1.5 bits` in `RX_START_US` is the standard trick: wait one and a half
bit times from the falling edge and you land in the *middle* of bit 0, which
is the most tolerant sampling point.

**The RX path is deaf while transmitting.** `uartWrite()` holds `cli()` for
the whole byte. Anything the modem sends during a transmission is lost. In
practice the modem does not talk while you are typing a command, so this
matters only for unsolicited results — see
[known limitations](#known-limitations).

---

## OSCCAL calibration

The ATtiny85's internal RC oscillator is factory-trimmed to ±10% over
temperature and voltage. At 9600 baud, ±10% is fatal: by the stop bit the
sampling point has walked out of the bit entirely.

So the firmware tunes itself against the modem:

1. Try the factory `OSCCAL`. Send `AT`, look for `OK`.
2. If that fails, walk `OSCCAL` down, then up, retrying at each step.
3. Store the value that worked in EEPROM behind a magic byte, so the search
   happens once in the product's life rather than at every boot.

Two details here are easy to get wrong and this code gets right.

**`OSCCAL` is not a linear scale.** It is two overlapping ranges, 0x00–0x7F
and 0x80–0xFF, and stepping across the 0x7F/0x80 boundary jumps the frequency
discontinuously. The search clamps to whichever half the factory value lives
in:

```c
if (factory < 0x80 && hi > 0x7F) hi = 0x7F;
if (factory > 0x7F && lo < 0x80) lo = 0x80;
```

**`OSCCAL` must be changed one step at a time.** Writing a distant value in
one go can glitch the clock mid-instruction. `tryOsccal()` increments or
decrements toward the target with a short settle:

```c
while (OSCCAL != target) {
    if (OSCCAL < target) OSCCAL++; else OSCCAL--;
    _delay_us(50);
}
```

Using the modem as the frequency reference is the neat part: it needs no
crystal, no second timer and no external signal. The thing you must talk to
is the thing that tells you whether you can talk.

*Minor inefficiency:* the upward search starts from `lo` rather than from
`factory`, so it re-tests values the downward search already tried. Harmless
— it costs a few hundred milliseconds once — but it is not intentional.

---

## Why `CVHU=0` matters

This is the whole reason v11 exists, and it is worth knowing because the
symptom is baffling.

On much SIM900A firmware the default is `AT+CVHU=1`, under which **`ATH` is
accepted and then ignored for voice calls**. The modem answers `OK`. The call
keeps ringing. Every diagnostic says the hang-up succeeded.

The fix is two-part:

```c
gsmCmd(C_CVHU, 600);   /* AT+CVHU=0 - make ATH actually hang up */
...
gsmCmd(C_CHUP, 600);   /* SIMCom's dedicated disconnect, tried first */
gsmCmd(C_ATH,  400);   /* ATH as the fallback                        */
```

`AT+CHUP` is SIMCom's own disconnect and does not depend on `CVHU`. Issuing
it first and keeping `ATH` as a fallback covers both firmware behaviours.

And because a single missed hang-up leaves the caller ringing, any `RING`
arriving after the action re-issues it:

```c
if (!strncmp(line, "RING", 4)) {
    if (callDone) pendingHangup = true;   /* still ringing: cut again */
    else          tryCallAction();
}
```

---

## Flags, never transmit, in the parser

`onLine()` sets flags and returns. It never sends an AT command.

That is a deliberate constraint and it prevents a specific class of bug.
Transmitting takes ~1 ms per byte with interrupts disabled; doing it from
inside the receive path means the RX buffer is not being drained while you
transmit, and a burst of `RING` / `+CLIP` / `+CMT` lines arrives while the
firmware is deaf. Worse, `sendSMS()` waits for a `>` prompt, and calling it
from the parser could re-enter the parser.

So the parser records intent — `pendingHangup`, `pendingToggle`,
`pendingAction`, `pendingDelete` — and `loop()` acts on it, in priority
order, with hang-up first. Same pattern as an ISR that queues rather than
works.

---

## Security: caller ID is not authentication

**The only check on a missed call is the caller ID string, and caller ID is
spoofable.**

`+CLIP:` reports what the network says the calling number is, and on most
networks that value can be forged by anyone with a VoIP provider willing to
pass an arbitrary CLI. The cost is a few pence.

So the honest description is:

> This is a convenience control, not a security control. Anyone who knows the
> authorised number and can spoof it can toggle the output.

That is acceptable for a light, a fan, or a pump in a private yard. It is
**not** acceptable for a door lock, a gate, a safe, or anything whose
unexpected activation is dangerous or expensive.

If you need a real control, the options in increasing order of effort:

| Approach | Cost | What it buys |
| :--- | :--- | :--- |
| Require an SMS containing a shared secret | trivial | Defeats casual CLI spoofing; the secret still crosses the network in clear and sits in the sender's outbox |
| Rolling code — HMAC over a counter, truncated to 6 digits, in the SMS body | moderate; needs the counter in EEPROM and a shared key | Defeats replay and spoofing |
| Challenge-response over SMS | higher latency | Defeats replay without a synchronised counter |

The rolling-code approach is the sensible middle. The primitives are in
[security-engineering](https://github.com/ChitranshBaregama/security-engineering) —
HMAC-SHA-256 and a constant-time comparison — though on an ATtiny85 with
3.9 KB already used you would want a smaller MAC.

**Also note:** the authorised number is compiled in as a `#define`. Changing
it means reflashing. That is a reasonable trade for a single-user device, and
a genuine limitation for anything else.

---

## Known limitations

Stated plainly, because the alternative is rediscovering them in the field.

**1. Unsolicited results are lost during an SMS send.**
`waitSendOutcome()` blocks for up to 45 seconds, draining the RX buffer and
discarding anything that is not `+CMGS` or `ERROR`. A call arriving during a
send is dropped entirely. Acceptable for a device that acts a few times a
day; not acceptable if commands can arrive back to back.

**2. The RX path is deaf while transmitting.** See
[bit-banging](#why-bit-bang-the-uart). Structural, and the price of a
software UART with no hardware buffer.

**3. `expectBody` has no timeout.**
`+CMT:` announces that the *next* line is the message body. If that body
never arrives — a dropped byte, a modem reset mid-message — the flag stays
set and the next unrelated line is consumed as a body. A `RING` arriving at
that moment is swallowed and the call is missed.
[Pinned by a test](../tests/test_protocol.cpp) so it cannot regress silently.
*Fix:* clear `expectBody` if the next line does not arrive within a second or
two, or if it starts with a known URC prefix.

**4. `line[64]` truncates long messages.**
The command must appear within the first 63 characters of the SMS body.
Longer messages are cut. Raising it costs RAM, which is 55% used.

**5. No delivery confirmation.**
`waitSendOutcome()` returns on `+CMGS` — the modem accepted the message for
sending. It does not mean it was delivered. There is no retry on failure, by
design: retrying an SMS that may have been sent is worse than not retrying.

**6. `millis()` and the 49-day wrap.**
All timing uses `millis() - t0 < interval`, which is the overflow-safe form
and wraps correctly. No issue — noted because it is the first thing a reviewer
checks.

**7. `millis()` accuracy during transmission.**
`uartWrite()` disables interrupts for ~1.04 ms per byte. Timer0 overflows
every 2.048 ms at 8 MHz with a /64 prescaler, so at most one overflow can be
pending during a `cli()` window and the interrupt flag preserves it. No ticks
are lost. If the baud rate were lowered or the prescaler changed, this would
need re-checking.

---

## Resource budget

ATtiny85: 8 KB flash, 512 B RAM, 512 B EEPROM.

```
Program:    3934 bytes (48.0% Full)
Data:        280 bytes (54.7% Full)
```

RAM is the tighter constraint, and the breakdown is roughly:

| | bytes |
| :--- | ---: |
| `rxBuf[64]` — the UART ring buffer | 64 |
| `line[64]` — the current line being assembled | 64 |
| state variables, flags, timers | ~40 |
| stack, at its deepest | the rest |

Every AT command and every message string lives in `PROGMEM`, fetched with
`pgm_read_byte`. Without that, ~400 bytes of string constants would be copied
into RAM at startup and the build would not fit.

The `avr-libc` build with the compat shim is **3934 bytes**. The same sketch
through the Arduino IDE is larger, because the core brings in its own `main`,
timer setup and `init()`. On a part where half the flash is already gone, that
difference is worth having.
