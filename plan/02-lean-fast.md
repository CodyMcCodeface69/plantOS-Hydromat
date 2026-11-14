# 02 Lean and fast

## Intent
Less flash, less RAM churn, a responsive main loop and code that can be read in pieces. The
flash headroom gained here pays for the presets (03) and possibly a custom UI (04).

## Constraints
- Behavior must stay the same unless a change is deliberate and documented (`FSMINFO.md`).
- You can't measure on the device right now. Flash and RAM numbers come from the build output.
  Loop time has to be argued from the code. Record the before and after figures in `LOG.md`.
- Baseline: Flash 86.3 % / 1,583,438 B, RAM 39,256 B static (production build, `99a181e`).
- The `plantos/vitals` MQTT payload is a contract with the desk display and Grafana.

## Starting points (see `WEAKNESSES.md`)
The size of the controller and YAML lambdas, log volume and strings, the cost of `CentralStatusLogger`, `delay()` in
`ezo_ph_uart`, and the unused 8 MB flash (USB flash needed, so prepare only).
Which of these pay off is your call.

## Done when
- A measurable flash reduction, with numbers in `LOG.md`.
- No regressions in what the web UI exposes, or deliberate removals listed in `LOG.md`.
