# Wiring

```
        ATtiny85                       SIM900A
      ┌───────────┐                  ┌──────────┐
  PB5 │1 RESET  8 │ VCC ── +5 V      │          │
  PB3 │2        7 │ PB2              │          │
  PB4 │3        6 │ PB1 ◄──────────── TXD       │
  GND │4        5 │ PB0 ────────────► RXD       │
      └───────────┘                  │          │
         │                           │      GND │
         └───────────── common GND ──┴──────────┘
```

| ATtiny85 | Direction | Connects to | Notes |
| :--- | :---: | :--- | :--- |
| PB0 (pin 5) | → | SIM900A **RXD** | Bit-banged TX |
| PB1 (pin 6) | ← | SIM900A **TXD** | Bit-banged RX, on PCINT1 |
| PB2 (pin 7) | → | **D1** LED + 220 Ω → GND | ON indicator, 5 s pulse |
| PB3 (pin 2) | → | **D2** LED + 220 Ω → GND | System state, latched |
| PB4 (pin 3) | → | **D3** LED + 220 Ω → GND | OFF indicator, 5 s pulse |
| PB5 (pin 1) | — | RESET | **Do not use.** Repurposing it needs the RSTDISBL fuse, after which the part can only be recovered with a high-voltage programmer |
| Pin 8 | — | VCC, +5 V | |
| Pin 4 | — | GND | |

---

## Power — read this before anything else

| Rail | Requirement |
| :--- | :--- |
| SIM900A | **4.0–4.2 V, 2 A capable.** Not 5 V, not from the ATtiny85, not from a USB port |
| ATtiny85 | 5 V or 3.3 V, trivial current |
| Ground | **Common between both.** Mandatory |

The SIM900A draws burst currents up to 2 A while registering on the network
and while transmitting. An undersized supply sags, the modem browns out and
resets, and the symptom looks like flaky firmware — the modem stops answering
mid-command and the ATtiny85 sits in `waitForModem()`.

A bulk capacitor (≥1000 µF) close to the modem's supply pins absorbs the
bursts and is worth fitting even with an adequate supply.

**Level shifting:** the SIM900A's logic is 2.8 V. Driving its RXD from a 5 V
ATtiny85 is out of spec. Many modules include a level shifter or tolerate it
in practice; check your specific board. A simple divider (1 kΩ / 2 kΩ) on
PB0 → RXD is cheap insurance. The modem's 2.8 V TXD is read reliably as a
logic high by a 5 V AVR, so that direction needs nothing.

---

## LED sense

All three LEDs are active-high: the pin drives the anode through 220 Ω to
ground. At 5 V that is roughly 15 mA per LED, within the ATtiny85's 40 mA
per-pin limit, but note the **total** limit across the port — do not assume
all three plus the UART pin can source maximum current simultaneously.

## RING sequence

A SIM900A announces an incoming call like this:

```
RING
+CLIP: "+919876543210",145,"",0,"",0
RING
+CLIP: "+919876543210",145,"",0,"",0
```

Ring 1 is counted and its `+CLIP` authorises the caller; ring 2 fires the
action, so the call is cut on the second ring. Set `RINGS_TO_ACT` to 1 to cut
on the first — quicker, but it gives the caller no chance to hang up first if
they dialled by mistake.

Some firmware emits `+CLIP:` *before* the first `RING`. The parser handles
that by seeding the ring counter when `+CLIP` arrives first.
