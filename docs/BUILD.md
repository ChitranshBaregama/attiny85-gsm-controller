# Building and Flashing

Two paths. They compile the **same, unmodified** `.ino` — there is no second
copy of the source to drift.

---

## Arduino IDE

1. Install **ATTinyCore** (Board Manager URL:
   `http://drazzy.com/package_drazzy.com_index.json`).
2. Board: **ATtiny85 (No bootloader)**
3. Clock: **8 MHz (internal)**
4. **Burn Bootloader** once — this sets the fuses. Without it the part runs
   at 1 MHz and nothing works (see [fuses](#fuses)).
5. Open `firmware/ANANT_IONS_SIM900A_ATTINY85_V11.ino`, set `USER_NUMBER`
   and `USER_MATCH`, and upload with a USBasp or an Arduino as ISP.

## Command line, no Arduino installation

```bash
sudo apt install gcc-avr avr-libc avrdude

make -C firmware            # compile, report flash and RAM usage
make -C firmware hex        # produce gsm-controller.hex
make -C firmware fuses      # print the fuse settings and the avrdude line
```

`compat/` supplies `millis()` and `main()`; everything else the sketch needs
comes from avr-libc. This is what CI builds, so the repository cannot claim
to compile when it does not.

Flashing:

```bash
avrdude -c usbasp -p t85 -U flash:w:firmware/gsm-controller.hex:i
```

---

## Fuses

```
lfuse 0xE2    hfuse 0xDF    efuse 0xFF
```

```bash
avrdude -c usbasp -p t85 -U lfuse:w:0xE2:m -U hfuse:w:0xDF:m
```

**`CKDIV8` must be off.** It is *on* from the factory. With it on the part
runs at 1 MHz, every bit-banged delay is eight times too long, and the modem
sees noise. The `#error` guard in the sketch catches a wrong `F_CPU` at
compile time; it cannot catch a wrong fuse, so this is the step to check
first when a fresh board does nothing.

---

## Tests

```bash
make -C tests
```

Compiles the real sketch against a host shim that replaces the AVR registers
with ordinary variables, then drives the parser with the lines a SIM900A
actually emits. 44 checks, no hardware.

What it covers: the line parser, call state machine, authorisation, SMS body
handling, command de-duplication, registration parsing.
What it cannot cover: bit-banged UART timing, OSCCAL calibration, and
anything analogue. Those need the part and a scope.

---

## First bring-up checklist

In order. Each step eliminates roughly half of what can be wrong.

1. **Power.** The SIM900A draws up to **2 A** in bursts during network
   registration. An undersized supply browns it out and it resets silently
   mid-command. Give it its own 4.0–4.2 V supply, and share ground with the
   ATtiny85. Most "the code does not work" reports are this.
2. **Fuses.** `CKDIV8` off, 8 MHz internal.
3. **Modem alone.** Before involving the ATtiny85, talk to the SIM900A from a
   USB-TTL adapter at 9600. Confirm `AT` → `OK`, then `AT+CREG?` → `,1` or
   `,5`. If that does not work, nothing downstream will.
4. **Wiring.** PB0 → modem RXD, PB1 ← modem TXD. Crossed, not straight.
5. **Set `CAL_BLINK` to 1** for a first bring-up. D3 blinks once per OSCCAL
   step, so you can see calibration searching rather than guessing whether it
   hung.
6. **Watch for the boot SMS.** "GSM READY" means the whole chain works:
   clock calibrated, modem responding, network registered, SMS sending.
