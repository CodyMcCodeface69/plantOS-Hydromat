# Values to measure on site

The owner works through this list once back at the device. Every placeholder in the presets gets a
row here. Mark measured rows as done (☑) and enter the values where the "Where" column says.

| ☐ | Preset | Value | Placeholder | Where to enter | How to measure |
|---|---|---|---|---|---|
| ☐ | Mutter | Tank volume LOW→HIGH (L) | _tbd_ | _tbd_ | Water level at the LOW sensor, fill to HIGH, measure the added liters (measuring jug or valve runtime × 100 mL/s) |
| ☐ | Mutter | Tank volume EMPTY→HIGH (L) | _tbd_ | _tbd_ | Same, starting at the EMPTY sensor |
| ☐ | Anzucht | Tank volume LOW→HIGH (L) | _tbd_ | _tbd_ | as above |
| ☐ | Anzucht | Tank volume EMPTY→HIGH (L) | _tbd_ | _tbd_ | as above |

| ☐ | Mutter | Calendar pH window | 5.8–6.2 | `plantOS.yaml` → `calendar_manager: calendars: mutter: phases[0]` `ph_min` / `ph_max` (reflash) | Log pH over 1–2 weeks with the mother plants healthy; keep the window where no correction is needed most days. Nutrient chart of the fertilizer for "vegetative/mother" |
| ☐ | Mutter | Calendar nutrient doses A/B/C | 1.6 / 1.6 / 1.2 mL/L (Haupt days 24–28) | same phase, `dose_A_ml_per_L` / `dose_B_ml_per_L` / `dose_C_ml_per_L` | Fertilizer chart for mother plants; after one full feed measure the tank EC: it should land near `ec_target` |
| ☐ | Mutter | Calendar EC target / tolerance | 1.6 / 0.2 mS/cm | same phase, `ec_target` / `ec_tolerance` (mS/cm) | Handheld EC meter in the mother tank over a few weeks; target = EC at which the plants stay green without tip burn |
| ☐ | Mutter | Calendar light window (night mode) | 16:00–10:00 (18 h) | same phase, `light_on_time` / `light_off_time` ("HH:MM") | Mutter has no grow light: the window only decides "night" for night mode. Enter the real light times of the mother chamber; set both equal to use the night-mode hour window instead |
| ☐ | Anzucht | Calendar phases and length | "Keimung" day 1–7, "Bewurzelung" from day 8, `days: 21` | `calendar_manager: calendars: anzucht:` `days`, `phases[*].from_day`, `name` | Days from sowing/cutting to roots visible, and to transplanting into Haupt |
| ☐ | Anzucht | Calendar pH window | 5.8–6.2 (both phases) | `anzucht: phases[*]` `ph_min` / `ph_max` | as for Mutter, in the propagation tank |
| ☐ | Anzucht | Calendar nutrient doses and EC | Phase 1: 0 mL/L, EC off (`ec_target: 0`); phase 2: 0.4 / 0.4 / 0.4 mL/L, EC 0.5 ± 0.1 mS/cm | `anzucht: phases[*]` `dose_*_ml_per_L`, `ec_target`, `ec_tolerance` | Only used if Anzucht gets nutrient pumps. Fertilizer chart for seedlings/cuttings; EC meter after a feed |
| ☐ | Anzucht | Calendar light window | 16:00–08:00 (16 h, as Haupt day 1) | `anzucht: phases[*]` `light_on_time` / `light_off_time` | Light times of the propagation lamp |

_(Rows for the actuator sets of each preset follow with step 3.3.)_
