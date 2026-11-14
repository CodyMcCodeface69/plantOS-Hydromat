# Lessons from past crashes (ESP32-C6, RISC-V)

Condensed on 2026-11-14 from four fix write-ups of January 2026 (`docs/fixes/*`, now in git
history, last present in commit `5f549e0`). Only the parts that still apply are kept.

## 1. Alignment: "Store address misaligned" (MCAUSE 0x7)

**What happened.** The device rebooted with `MCAUSE: 0x00000007` in `xQueueGiveFromISR`.
The cause was the PSM struct `CriticalEventLog`, which had `char eventID[32]` in front of an
`int64_t`. Removing log calls from sensor callbacks was done first and did **not** fix the crash;
reordering the struct did.

**Rules.**

- RISC-V traps on misaligned access. Xtensa (classic ESP32) did not, so code that "worked on an
  ESP32" proves nothing here.
- Order struct members from largest to smallest. Structs that go to NVS or through a FreeRTOS
  queue also get `__attribute__((aligned(8)))`. The reference is `CriticalEventLog` in
  `components/persistent_state_manager/persistent_state_manager.h`.
- Do not use `__attribute__((packed))` on structs with 64-bit or float members.
- Changing a struct that is stored in NVS changes its binary layout. Old records become
  unreadable. Use a new NVS key (the code does this, e.g. `EcKv2`) or accept a reset of that value.

**How to recognise it.** `MCAUSE 0x7` (store) or `0x5` (load), a fault address that is not a
multiple of 4, often inside a queue or NVS call. Decode addresses with `task decode -- 0x...`.

## 2. No logging or heavy work in callbacks, ISRs and worker tasks

ESPHome sensor callbacks, ISRs and the Shelly worker task do not run in the main loop context.

- No `ESP_LOG*`, no `std::string` building, no blocking calls there.
- Store the value, set a flag, and log from `loop()`.
- The main thread and the Shelly worker task talk only through the two queues; results are
  applied in `ESPHomeHAL::loop()`.

## 3. Watchdog: nothing may block the main loop

**What happened.** Task-watchdog resets came from `delay(300)` calls in the periodic EZO pH read
and from HTTP timeouts to an unreachable Shelly inside the loop.

**State today (checked against the code).**

- The periodic pH read (`update()` / `loop()` in `ezo_ph_uart.cpp`) is a non-blocking state machine.
- The **command paths** of `ezo_ph_uart.cpp` still call `delay()`: calibration, continuous-mode
  on/off, temperature compensation (50 ms each) and `wait_for_response_()` (300 ms). This is known
  tech debt (step 2.6 of the overhaul plan). Do not add more.
- Shelly HTTP runs in its own FreeRTOS task with a 2 s timeout, so a dead Shelly cannot stall the loop.
- `wdt_manager` in `plantOS.yaml`: timeout 20 s, fed every 500 ms.

**Rules.** No `delay()` in `loop()` paths; use `millis()` deltas and small state machines.
Network I/O belongs in the HAL worker pattern. A watchdog reset shows up in the boot log as
`task_wdt: Task watchdog got triggered`.
