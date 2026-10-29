# Machine and Product

benchtop runs a **product**. A product is one or more **boards** plus the parts around them, wired
together and driven by one clock. A **machine** is one board. This page names the types, and gives
the order in which the module-level globals move into them.

## Machine: one board

```sysl
struct Machine
    map: MemoryMap            // where the board puts RAM and flash (v6m)
    memory: &Memory           // the storage behind them, which the machine owns
    cores: [2]Cpu             // the chip's cores; each points at `memory` (Cpu.mem)
    configs: [2]CoreConfig    // what the description makes of each core
    devices: Buf[&Peripheral] // the chip's peripherals on the bus, each owning its state
    parts: Buf[&Part]         // parts soldered to the board (the Pico 2 W's LED, the DISCO's panel)
    header: Buf[Pin]          // named header pins: the board's only external interface
    clock_hz: u64
```

- **A machine owns its state.** No machine field is reachable as a global. Two machines in one
  process (two boards in one product, or two tests on one thread) share nothing.
- **A core is the raw tier.** `Cpu.mem` is a `*Memory`, and the hot paths read it beside `pages`,
  `lines` and `dec`. The owning `&Memory` is the machine's. A core never outlives its machine.
- **State that crosses threads lives in a `Mutex[T]` that owns its data**, for example
  `Mutex[WorkerControl]` holding `paused`, `busy`, `step_pending` and `generation`. Short holds use
  `with`, and `lock_raw` appears only where `Condvar.wait` needs it. A `Mutex.new(0)` beside the
  globals it guards does not count as ownership.
- **`assemble` plans, then applies.** `plan(board) -> Result[Plan, string]` reads the description
  and sets nothing. The apply step cannot fail. A refused description leaves the machine as the last
  board left it.

`Pin` is `{ name: string, gpio: u32 }`: a header pin is a name (`GP15`, `3V3`, `D7`) bound to the
chip's pad.

## Product: boards, parts, wiring, one clock

```sysl
struct Product
    name: string
    boards: Buf[BoardSlot]   // `pico2` names a built-in board; `name` is how wiring refers to it
    parts: Buf[&Part]        // off-board parts: a display, buttons, a breadboard module
    wiring: Buf[Wire]        // editable at run time: moving a jumper is a Wire change
    scheduler: Scheduler     // the one clock every board and part advances on
    view: ProductView

struct BoardSlot
    name: string
    machine: &Machine

struct Wire
    from: PinRef             // `pico.GP15`, or a part's pin `oled.SDA`
    to: PinRef

enum ProductView
    Enclosure(view: View)    // a case drawn over the boards (the hidden-board product)
    Open(view: View)         // the boards themselves, drawn whole, with the parts beside them (Pinova)
```

- A product file names its boards by built-in name (`pico2`, `stm32h747-disco`), adds parts, and
  lists wires. It never edits a board file.
- **Parts work the same on a board or in a product.** A part sees only its pins, through one
  `Part` trait (the device/part interface is slice 4).
- **Views.** `Open` reuses the `product-view` branch's model: an element whose `show` names a board
  draws it whole (`Shows.ShowBoard`), and the window draws a flat `Drawing` of `Drawn` items, each
  carrying the board's turn and offset. "Open the case" switches an `Enclosure` product to the board
  view of the same machines. The product keeps running underneath.
- First target: **Pinova P2**, which is `pico2` (a Pico 2 W, with CYW43 stubbed for the LED and "no
  network") plus a carrier board's labelled headers, plus modules on jumpers.

## Slices

1. **Memory map** (done): `v6m.Memory` holds the map, the RAM, flash, page table, line map and
   decoded records. `Cpu.mem` points at it. `virt.Machine { map, memory }` owns the board's memory,
   and `assemble` checks the whole description (memory map, bus, clocks, cores) before setting
   anything.
2. **Devices** (done). Each device file's state is that device's struct (`AdcState`, `SpiState`,
   `AudioState`, ...), and `Machine.devices` (`Devices`, in `virt/devices.sysl`) owns one of each,
   with the bus (`BusState`). Each device has a check, `check_*(board) -> Result[State, string]`,
   that reads the description and sets nothing, and `apply_devices` puts the checked set in place
   and cannot fail; so a refused description leaves every device as the last board left it. A
   device's window on the bus is the registry's where the chip gives it no size. The locks the
   devices are read under, and the queues and threads that carry their data to the host (the SD
   image writer, the DSI worker, the SAI and codec sample queues), stay module-level for slice 3.
3. **Scheduler and threads.** One scheduler, owned by the product. The worker, parallel and pump
   globals move into it. Cross-thread state moves into `Mutex[T]` values that own it: the device
   locks with the device state they guard, and the host-facing device queues.
   - Done: `Scheduler` (`virt/scheduler.sysl`) holds the board's clock, the quantum, launch state,
     the lockstep pairing (`Lockstep`, atomics) and the board's lock (`Mutex[u64]`, owning the clock
     both sleeping cores skip to), and the CPU workers (`Workers`, `virt/workers.sysl`), whose
     shared control state is one `Mutex[WorkerControl]`. It runs a board in turns (`run_board`,
     `pump_board`), in lockstep (`set_threads`) or paced on the workers, and one `pair_cores`
     readies both lockstep modes. `threads` is split into the request (`run_on_threads`) and the
     live state (`threaded`). The desktop's old bounded-batch path (`pump_parallel`) is gone; its
     tests run on the workers. It is still one module-level `scheduler` until the product exists.
   - Done: a device more than one thread reaches keeps what the board fixes (whether the bus has
     it, its IRQs, its clock) and the atomics it publishes for lock-free readers beside a `Mutex`
     that owns everything else: `adc.live`, `rp.live` (taken on every access, whichever thread),
     `rcc.gcr`, `pwm.live`, `i2s.controller`, `i2c.live` (with the touch controllers), `displays.live`,
     `spi.serial` (which the panel GPIO's writes take too, to stay in order with serial edges),
     `dsi.host`, `audio.live` (with the SAI's host queues) and the codec's `input` and `mix`. The DSI
     worker's progress is a `Mutex[DsiProgress]`. `apply_devices` parks every thread that reaches
     a device and then replaces the whole set, locks included.
4. **Delete the remaining globals.** A `Peripheral` trait (`read`, `write`, `advance`, `due`,
   `reset`) replaces the closed `enum Device` and its dispatch sites, over the device structs.
   `first`, `second`, the SIO FIFOs, the console and `virt.machine` itself go. Every entry point
   takes a `*Machine`.
   - Done: the storage path and the console. `StorageState` keeps what the board fixes and holds
     the SPI controller, the GPIO banks and an SPI card in `live: Mutex[StorageLive]`; the SDMMC
     host and its card are `sdmmc.live: Mutex[SdmmcLive]`, and `check_storage` hands a card on the
     SDMMC to the SDMMC (`(StorageState, Option[&sd.Card])`). Where both are held, the SDMMC's lock
     is taken first, never the reverse. The SD image is one `&sync SdImage`: its bytes in a
     `Mutex[Buf[u8]]`, and its writer (the sector queue, the frozen file name, atomic `failed` and
     `pending`, the thread) beside them; `sd_image()` answers a `&sync Frozen[u8]` copy. The
     console is `Machine.console: Console` (its channel, an atomic `ctrl` and
     `live: Mutex[ConsoleLive]`), and `output()` answers a `&sync Frozen[u8]`. The DSI worker is
     `Scheduler.dsi: DsiWorker`. `storage_lock` and `console_lock` are gone.
   - Done: the cores. `Machine.cores: [2]Cpu` replaces `first` and `second`; `core0()` and
     `core1()` answer `&machine.cores[k]`. A core reaches its machine through `Cpu.bus: *Bus`, a
     v6m `trait Bus` that `Machine` implements and `boot` and `launch` point at, in place of the
     `io_read`/`io_write` function globals. The trait call costs at most about 1% in host
     instructions on the interpreter and board benches.
   - Done: the cores' configuration. `Machine.configs: [2]CoreConfig` holds what the description
     makes of each core (model, instruction timing, NVIC priority bits, DSP, `Fpu`, and its clock's
     divisor of the chip's), and `Machine.clock_hz` the chip's clock; `boot` and `launch` give a core
     its configuration with one `configure`. Each core's last bus window is `Machine.bus_cache`, a
     `BusCache` aligned to `HOST_CACHE_LINE` (128 bytes, Apple silicon's line) so the two cores'
     threads never write one line.
   - Done: the SIO. Its FIFOs, spinlocks and the boot ROM's place in the launch sequence are
     `Machine.devices.sio` (`virt/sio.sysl`).
   - Done: the dispatch. Two traits (`virt/peripheral.sysl`): a `Peripheral` (`boot`, `due(cpu)`,
     `wake(cpu)`, `signal(cpu)`) is a device of the machine's, and a `BusWindow` (`read`, `write`) a
     window of one on the bus. `attach` points each of the bus's windows at the device that answers
     it and lists the devices the board has, so an access is one trait call, `boot` a loop over every
     device, and a slice's deadline a fold over the running ones. `enum Device` remains the kind a
     description names a device by; a device that answers several windows from one state (the SAI
     and DMA block, the STM32 SPI and GPIO, the RP2040's resets, timer and watchdog) is reached
     through a small window value that carries it.
   - Left: `virt.machine` itself. The devices' functions still reach `machine.devices.X` rather
     than their own `self`, and the entry points do not yet take a `*Machine`.
   - Done: RISC-V harts on a board. A chip's devices see a core as a `Processor`
     (`processor/processor.sysl`: its number, its retired count, its interrupt inputs), which
     `v6m.Cpu` and `riscv.Cpu` both implement. The RP2350's devices are core-free models
     (`peripheral/rp2350`) at the SVD's addresses, and `chip.Rp2350` decodes the bus, runs the clock
     tree and the boot ROM's core-1 launch protocol, and raises interrupts through `Processor`.
     `chip.RiscvBoard` runs the two Hazard3 harts on it (`boards/pico2-riscv`, `benchtop run`).
   - Left: the virt devices' handlers still take a v6m `*Cpu`; moving them onto `Processor` is
     mechanical once `virt.machine` is gone. The RISC-V board is not yet a `Machine` (no window,
     no workers), and the Arm Pico 2 does not yet run on `chip.Rp2350`.
5. **Headers and Product.** `Pin` and `header` on boards, product files with wiring, run-time wire
   edits, and the product view.
