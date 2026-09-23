# ATtiny85 + SIM900A GSM Controller

[![build and test](https://github.com/ChitranshBaregama/attiny85-gsm-controller/actions/workflows/ci.yml/badge.svg)](https://github.com/ChitranshBaregama/attiny85-gsm-controller/actions/workflows/ci.yml)
[![licence](https://img.shields.io/badge/licence-MIT-lightgrey)](LICENSE)

Switch something on and off by **missed call** or **SMS**, from an ATtiny85
with eight pins and no hardware serial port.

The interesting constraint is that an ATtiny85 has no USART. The UART here is
bit-banged on two pins at 9600 baud, and the internal RC oscillator — which is
nowhere near accurate enough for that out of the factory — **calibrates itself
against the modem** at first boot and remembers the result in EEPROM.

Resource usage is reported by `make -C firmware`. Static RAM excludes the
stack; physical UART timing and oscillator behavior require hardware tests.

---

## What it does

| Input | Result |
| :--- | :--- |
| Power on | All outputs off, SMS "GSM READY" to the authorised number |
| **Missed call** from the authorised number | Cut the call on the 2nd ring, **toggle** the output, SMS the new state |
| Missed call from anyone else | Cut immediately, ignore |
| **SMS "SYSTEM ON"** | D1 pulses 5 s, D2 latches on, reply "SYSTEM ON" |
| **SMS "SYSTEM OFF"** | D3 pulses 5 s, D2 latches off, reply "SYSTEM OFF" |

The missed call is free — the caller hangs up, or rather the device hangs up
for them, before it connects.

---

## The parts worth reading

**Bit-banged UART with corrected timing.** At 8 MHz a bit is 833 cycles, and
the sampling loop's own overhead is a measurable fraction of that. The
constants subtract it:

```c
#define TX_BIT_US     (BIT_US - 1.6)
#define RX_BIT_US     (BIT_US - 2.4)
#define RX_START_US   (BIT_US * 1.5 - 6.0)   /* 1.5 bits -> mid-bit 0 */
```

**Self-calibrating clock.** The internal RC oscillator is ±10% from the
factory, which is fatal at 9600 baud. At first boot the firmware walks
`OSCCAL` until the modem answers `AT` with `OK`, then stores the value in
EEPROM. It respects the two details that trip people up: `OSCCAL` is two
overlapping ranges with a discontinuity at 0x7F/0x80, and it must be stepped
one value at a time.

**The `CVHU` trap.** On much SIM900A firmware `ATH` is *accepted and ignored*
for voice calls — the modem replies `OK` and the phone keeps ringing. The fix
is `AT+CVHU=0` at init plus `AT+CHUP` as the primary disconnect.
[Explained in full.](docs/DESIGN-NOTES.md#why-cvhu0-matters)

**The parser never transmits.** `onLine()` sets flags; `loop()` acts on them,
hang-up first. Transmitting from the receive path would leave the firmware
deaf during a burst of URCs and could re-enter the parser.

---

## Tested without hardware

```bash
make -C tests      # protocol regression checks, no chip or modem
```

The tests `#include` the **actual sketch** and compile it against a shim that
replaces the AVR registers with ordinary variables. Not a copy of the logic —
a copy would drift from the firmware within a month and quietly stop testing
anything.

They drive the parser with the lines a SIM900A really emits and check: the
two-ring sequence, an unauthorised caller being cut and ignored, `+CLIP`
arriving before `RING`, SMS authorisation, case-insensitive command matching,
command de-duplication inside and outside its window, `+CMTI` index parsing,
and registration states.

Regression tests cover repeated rings after an action, sender-field boundaries,
and recovery when an SMS body does not arrive within two seconds, including
timer wraparound. They exercise the parser and RX pump, not `setup()`, the full
`loop()`, physical UART timing, or modem hardware.
[Remaining limitations.](docs/DESIGN-NOTES.md#known-limitations)

---

## Security: read this before using it for anything that matters

**The only check on a missed call is the caller ID, and caller ID is
spoofable** for a few pence through any VoIP provider that passes an arbitrary
CLI.

> This is a convenience control, not a security control.

Fine for a light, a fan, or a pump. **Not** fine for a door, a gate, or
anything whose unexpected activation is dangerous or expensive.
[Options for making it real](docs/DESIGN-NOTES.md#security-caller-id-is-not-authentication)
— a shared secret in the SMS, or a rolling code — are set out in the design
notes.

---

## Documentation

| | |
| :--- | :--- |
| [**Build and flash**](docs/BUILD.md) | Arduino IDE and command line, fuses, and a first bring-up checklist |
| [**Wiring**](hardware/WIRING.md) | Pinout, the power requirement that causes most failures, level shifting |
| [**Design notes**](docs/DESIGN-NOTES.md) | Why each decision, what is right that is easy to get wrong, and every known limitation |

## Build

```bash
make              # host tests, then compile for ATtiny85
make -C firmware  # firmware only, with size report
make -C tests     # tests only
```

Needs `gcc-avr` and `avr-libc` for the firmware; a C++ compiler for the tests.
No Arduino installation required — `compat/` supplies `millis()` and `main()`,
and the same unmodified `.ino` builds both ways.

## Hardware

ATtiny85 · SIM900A · three LEDs with 220 Ω · a 2 A-capable 4.0–4.2 V supply
for the modem. **Common ground is mandatory**, and an undersized modem supply
is the cause of most reported failures.

## Related

- [**SMS P10 notice board**](https://github.com/ChitranshBaregama/sms-p10-notice-board) — the larger sibling: ATmega2560 + SIM800 driving a scrolling LED matrix
- [**Embedded systems reference**](https://github.com/ChitranshBaregama/embedded-systems-resources) — the UART and state-machine documents behind this design

## Licence

[MIT](LICENSE).
