# Values to measure on site

The owner works through this list once back at the device. Every placeholder in the presets gets a
row here. Mark measured rows as done (☑) and enter the values where the "Where" column says.

| ☐ | Preset | Value | Placeholder | Where to enter | How to measure |
|---|---|---|---|---|---|
| ☐ | Mutter | Tank volume LOW→HIGH (L) | 10.0 | Web UI, group "Preset": select "Preset" = Mutter, then number "Tank Volume LOW→HIGH" (stored in NVS for Mutter; YAML default `plantos_controller: presets: mutter: tank_volume_delta_liters`) | Water level at the LOW sensor, fill to HIGH, measure the added liters (measuring jug or valve runtime × 100 mL/s) |
| ☐ | Mutter | Tank volume EMPTY→HIGH (L) | 15.0 | same, number "Tank Volume EMPTY→HIGH" (YAML `presets: mutter: total_tank_volume_liters`). Enter EMPTY→HIGH first if the new LOW→HIGH is larger than the old EMPTY→HIGH (LOW→HIGH may not exceed it) | Same, starting at the EMPTY sensor |
| ☐ | Anzucht | Tank volume LOW→HIGH (L) | 5.0 | select "Preset" = Anzucht, number "Tank Volume LOW→HIGH" (YAML `presets: anzucht: tank_volume_delta_liters`) | as above |
| ☐ | Anzucht | Tank volume EMPTY→HIGH (L) | 8.0 | number "Tank Volume EMPTY→HIGH" (YAML `presets: anzucht: total_tank_volume_liters`) | as above |
| ☐ | Mutter | Function set | Nutrients + Water Fill + Drain (no Air Pump, no Grow Light) | Web UI, preset Mutter active: switches "Function Nutrients / Water Fill / Drain / Air Pump / Grow Light" (YAML `presets: mutter: functions`) | Check what is really wired to the mother tank: pumps A/B/C, valve + 3 level sensors, wastewater pump, Shelly socket 0 (air) and 3 (light). Switch off what is missing; a function that is on but not wired makes the controller run a pump into nothing |
| ☐ | Anzucht | Function set | none (pH only; owner question open in `QUESTIONS.md`) | preset Anzucht active: the five "Function ..." switches (YAML `presets: anzucht: functions`) | Same check for the propagation setup |
| ☐ | Haupt | Function set | all five | preset Haupt: "Function ..." switches | Confirm only; Haupt = today's production hardware |
| ☐ | all | pH / EC mixing time without air pump | factor 2.0 (pH mixing 30–600 s → 60–1200 s, EC mixing 300 s → 600 s; max 20 min) | `plantOS.yaml` → `plantos_controller: no_air_pump_settle_factor` (reflash) | In Mutter (no air pump): after an acid dose log the pH every minute (`task log`) until it stops moving by more than 0.02 per minute. Settle time ÷ the normal mixing time for that tank volume (`pH mixing N s` line) = factor. Same after a nutrient dose with the EC reading against 300 s |

| ☐ | Mutter | Calendar pH window | 5.8–6.2 | `plantOS.yaml` → `calendar_manager: calendars: mutter: phases[0]` `ph_min` / `ph_max` (reflash) | Log pH over 1–2 weeks with the mother plants healthy; keep the window where no correction is needed most days. Nutrient chart of the fertilizer for "vegetative/mother" |
| ☐ | Mutter | Calendar nutrient doses A/B/C | 1.6 / 1.6 / 1.2 mL/L (Haupt days 24–28) | same phase, `dose_A_ml_per_L` / `dose_B_ml_per_L` / `dose_C_ml_per_L` | Fertilizer chart for mother plants; after one full feed measure the tank EC: it should land near `ec_target` |
| ☐ | Mutter | Calendar EC target / tolerance | 1.6 / 0.2 mS/cm | same phase, `ec_target` / `ec_tolerance` (mS/cm) | Handheld EC meter in the mother tank over a few weeks; target = EC at which the plants stay green without tip burn |
| ☐ | Mutter | Calendar light window (night mode) | 16:00–10:00 (18 h) | same phase, `light_on_time` / `light_off_time` ("HH:MM") | Mutter has no grow light: the window only decides "night" for night mode. Enter the real light times of the mother chamber; set both equal to use the night-mode hour window instead |
| ☐ | Anzucht | Calendar phases and length | "Keimung" day 1–7, "Bewurzelung" from day 8, `days: 21` | `calendar_manager: calendars: anzucht:` `days`, `phases[*].from_day`, `name` | Days from sowing/cutting to roots visible, and to transplanting into Haupt |
| ☐ | Anzucht | Calendar pH window | 5.8–6.2 (both phases) | `anzucht: phases[*]` `ph_min` / `ph_max` | as for Mutter, in the propagation tank |
| ☐ | Anzucht | Calendar nutrient doses and EC | Phase 1: 0 mL/L, EC off (`ec_target: 0`); phase 2: 0.4 / 0.4 / 0.4 mL/L, EC 0.5 ± 0.1 mS/cm | `anzucht: phases[*]` `dose_*_ml_per_L`, `ec_target`, `ec_tolerance` | Only used if Anzucht gets nutrient pumps. Fertilizer chart for seedlings/cuttings; EC meter after a feed |
| ☐ | Anzucht | Calendar light window | 16:00–08:00 (16 h, as Haupt day 1) | `anzucht: phases[*]` `light_on_time` / `light_off_time` | Light times of the propagation lamp |

The K-factors (pH acid dose, EC feed) are global: values learned on one tank are reused after a
switch and the EMA adapts within a few corrections (owner question in the report of 2026-11-15).
