# PCM speaker and MEMS microphone

This flash-linked sysl demo uses a virtual I2S MCU peripheral, a MAX98357A
amplifier, a separate speaker (LS1), and an INMP441 MEMS microphone (MK1).
The component descriptions reserve four MCU pins for BCLK, WS, TX and RX.
Speaker input connects to `amplifier.out`, rather than directly to an MCU pin.

Hold keyboard **1** or click/hold SW1 for a 480 Hz triangular PCM tone. Speak
into the Mac's microphone to drive the five-LED level meter. The firmware reads
microphone samples and drives the GPIO LEDs; the UI does not calculate the meter.
Microphone input is not played back. Mute affects playback; pause freezes the
guest and discards stale host audio. No recording is written to disk.

```sh
./examples/audio/build.sh
sysl build .
./benchtop run --window --board boards/audio.hocon --watch \
  --build ./examples/audio/build.sh --watch-source examples/audio/main.sysl \
  examples/audio/demo.elf
```

SDL3 uses the Mac's CoreAudio playback and default recording devices. macOS may
request microphone permission. A missing/refused device leaves the UI usable;
without capture, the emulated microphone supplies silence. Headless runs never
open audio devices. Tests inject deterministic PCM or use SDL's dummy devices.

## MCU adapter

`virt-i2s` is a virtual register interface to a reusable frame-level I2S engine.
Its initial profile is 48 kHz, mono left channel, signed 24-bit audio in bits
31..8 of a 32-bit word, with a silent right slot. The MAX98357A and INMP441 models
support this common operating mode. Amplifier electronics, microphone acoustics,
PDM, stereo, pin-edge serial timing, DMA and IRQs are not modeled. Native RP2350
I2S-over-PIO firmware still requires a PIO/controller implementation; these are
not emulated RP2350 registers.

| Offset | Register | Behavior |
|---|---|---|
| 0x00 | CTRL | bit 0 TX enable, bit 1 RX enable, bit 31 resets FIFOs/flags |
| 0x04 | STATUS | bit 0 TX space, bit 1 RX data; bits 2..5 sticky W1C errors |
| 0x08 | TX | write a left-channel sample |
| 0x0c | RX | read and pop a left-channel sample |
| 0x10 | RATE | 48000, read-only |
| 0x14 | TX_SPACE | available slots in the 64-sample TX FIFO |
| 0x18 | RX_COUNT | readable samples in the 64-sample RX FIFO |
| 0x1c | LEVEL | absolute magnitude of the latest received sample |

Error bits: TX underrun (2), RX overflow (3), RX empty read (4), TX full write (5).
Underrun supplies silence. RX overflow drops new samples until firmware drains
its FIFO. Host capture/playback histories are bounded independently of these
hardware FIFOs. Rendering cadence does not determine the sample clock.

The generic catalog models are `i2s-amplifier`, `speaker` and `i2s-microphone`;
physical identities select reusable behavior. `speaker.gain` is a host debugging
parameter (0..100, default 12), not an electrical amplifier-gain register.

References:
- https://www.analog.com/en/products/max98357a.html
- https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf
