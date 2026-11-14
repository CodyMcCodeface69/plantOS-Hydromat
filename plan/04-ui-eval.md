# 04 Custom web UI evaluation

## Intent
Evaluate a **Shelly-style** web UI (a compact, task-oriented device page instead of ESPHome's
generic entity list): status at a glance, the main actions, presets and settings. The owner chose
**evaluation only**. The existing ESPHome UI stays as it is.

## Deliverable
`plan/ui-eval/` containing:
- A short assessment of the options: (a) a custom page served from the ESP, (b) external hosting
  (Hetzner, phone, PC) talking to the ESP's REST/SSE API, (c) staying with ESPHome's v3 frontend plus better
  grouping. Cover flash cost (86 % used today, 8 MB board possible after a USB flash), offline
  LAN use, maintenance across ESPHome versions, and security (the API has no auth today?).
- A recommendation.
- Optionally a static **prototype** (HTML/JS, no build step) that runs against the device's real
  REST/SSE endpoints. It can be opened locally once the owner is back in the LAN.

## Notes
- The ESPHome 2025.4 web_server REST/SSE API is the integration point. Its quirks are in
  `docs/WEBUI_PERFORMANCE_CLEANUP_PLAN.md` (SSE events without a `domain` field).
- Include the presets from `03` in the UI concept.
