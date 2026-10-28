# Audio, RTC and physical part identities

Board descriptions can name either a generic model or a real catalog part:

```hocon
buzzer { part = PS1240P02BT, ref = BZ1, gain = 12, wire { pin = GP5 } }
rtc { part = DS3231SN, ref = U2, initial-time = host, wire { i2c = i2c0 } }
```

The catalog preserves manufacturer and ordering identity separately from behavior.
TDK PS1240P02BT uses the generic passive-buzzer model; Analog Devices/Maxim
DS3231SN uses the DS3231 model. ST7789V, SSD1306 and HD44780U are also recognized
physical chip identities backed by the existing display models. Generic entries
remain available. Descriptions round-trip without replacing part numbers with
model names. Intrinsic properties, such as the TDK buzzer's passive drive type,
are fixed; instance settings such as host gain remain configurable. These are
software behavior models, not acoustic/electrical or packaging simulations.

The TDK identity is documented by its [manufacturer product entry](https://product.tdk.com/en/search/sw_piezo/sw_piezo/piezo-buzzer/info?part_no=PS1240P02BT).

## Buzzer and PCM playback

BZ1 is driven by a reusable `virt-pwm` MCU peripheral at `0x50000200`:

```hocon
pwm {
  peripheral = virt-pwm
  at = 0x50000200
  clock-hz = 12000000
  signals { out = [GP5] }
}
```

| Offset | Register | Behavior |
| --- | --- | --- |
| 0 | CTRL | Bit 0 enables output; bit 1 restarts phase. Enabling also starts phase. |
| 4 | PERIOD | Positive period in guest cycles; zero clamps to one. Writing restarts phase. |
| 8 | HIGH | High cycles per period, clamped to PERIOD. |
| 12 | COUNTER | Read-only phase counter. |
| 16 | PIN | Read-only configured GP pin number. |
| 20 | CLOCK_HZ | Read-only guest-cycle frequency. |

Only aligned 32-bit MMIO accesses are accepted. Invalid offsets are unmapped;
read-only writes are ignored. There is one PWM channel and no interrupt/DMA support.
When enabled, this channel supplies its selected pad independently of GPIO OUT/OE.
When disabled, the pad falls back to GPIO. This is a minimal static PWM routing
model, rather than the RP2040/RP2350 PWM register map or a complete chip pin mux.

Firmware samples RV1 and programs approximately 620–4000 Hz at half duty while
button 1 is held. The clock and waveform remain guest state: host playback never
programs the guest's pitch. The existing generic buzzer's active mode supports a
fixed tone gated by a GPIO; passive mode consumes the driven waveform. `tone` sets
an active buzzer's frequency (default 4000); `gain` is host gain, 0–100 (default 12).
The model does not reproduce resonance, sound pressure, load current or an enclosure.

PCM is the common host audio representation. A reusable renderer integrates the
waveform over each 48 kHz sample interval, including register changes partway
through a sample. It uses rational cycle/sample boundaries, removes DC and mixes
bounded mono buffers. Long stalls drop old audio rather than allowing unlimited
latency. This integration reduces sampling artifacts but is not a full acoustic
or reconstruction-filter simulation. Future DAC or I²S models can feed PCM into
the same playback path.

SDL3 queues float PCM on the default playback device; on macOS this is Core Audio.
The native app paces guest execution using the declared audio clock. Mute, pause,
reset and firmware reload clear stale queued playback; mute leaves firmware running.
Headless execution synthesizes bounded samples for tests and never opens an audio
device. Playback failure leaves the visual emulator usable and reports the error.

The existing LCD frees GP5 by declaring `write-only = true`, which means its RW
terminal is tied low. This is an ideal connection parameter; no resistor or supply
simulation is involved. The sysl driver keeps using the normal HD44780 write protocol.

## DS3231 RTC and I²C

The RTC implements its real 7-bit I²C address, `0x68`, time/date BCD registers,
12/24-hour formats, month and Gregorian leap/century rollover, day-of-week counter,
two alarm masks/flags, control/status, aging storage and a read-only quarter-degree
temperature result. Forced temperature conversion completes after a modelled
200 ms. START/repeated START snapshots protect multi-byte time reads from rollover.
Register pointers auto-increment and wrap. Alarm flags and OSF use write-zero-clear
semantics. Invalid BCD fields are ignored rather than corrupting the calendar.

RTC state lives only in RAM. A new instance defaults to `initial-time = host`,
copying the Mac's current local calendar once. It subsequently advances from that
stored time and accepts ordinary firmware register writes, without host-clock
resynchronization. LCD line one retains the greeting; line two shows the time
firmware reads through I²C. Button 2 demonstrates setting 2026-01-01 12:34:56
through those same I²C writes. Its LED still works. A portable driver can set any
valid supported date/time rather than that demonstration constant.

Use `initial-time = fixed` for repeatable simulations. Its parameters are
`initial-year` (2000–2199, default 2026),
`initial-month` and `initial-day` (default 1), and `temperature-quarter-c`
(default 100 = 25°C). For a fixed epoch the initial day-of-week value is 1; host seeding uses Sunday=1.
Firmware can assign its own numbering as on the device. Time advances deterministically with guest cycles,
freezes with the emulation, and survives MCU reset/reload. Rebuilding a board
creates a fresh RTC from its selected seed. There is no disk persistence, host-clock
resynchronization, disk-persisted battery,
supply-switching, oscillator drift, SQW/32 kHz pin waveform or routed alarm IRQ
behavior is implemented. The model exposes the alarm assertion (`interrupt`), but no
pin or interrupt input is wired to it.

The virtual MCU provides a byte-level I²C controller at `0x50000300`:

| Offset | Register | Behavior |
| --- | --- | --- |
| 0 | ADDRESS | 7-bit slave address. |
| 4 | DATA | TX/RX byte. |
| 8 | COMMAND | START=1, READ=2, WRITE=4, STOP=8. START+READ selects a read transaction; subsequent READ commands receive bytes. |
| 12 | STATUS | ACK=1, ERROR/NACK=2, ACTIVE=4. Write bit 1 to clear ERROR. |

A register read is address → START → pointer byte/WRITE → repeated START+READ →
READ bytes → STOP. Unknown addresses report NACK. Transfers complete immediately;
this controller does not model electrical SDA/SCL waveforms or arbitration. Its
`clock-hz` parameter relates guest cycles to RTC seconds, defaulting to 12 MHz.
The logical `buses { i2c0 {} }` connection requires no spare GPIOs. The current
backend supports one controller and one DS3231 address on it.

The model follows the [DS3231 manufacturer datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf).
A portable firmware driver belongs in its own package, with an MCU-independent
I²C interface and no Benchtop dependency. An embedded project supplies its MCU's
I²C implementation; Benchtop emulates the MCU controller and slave. This change
adds the emulator model and bus, not a separate `ds3231` firmware-driver repository.
