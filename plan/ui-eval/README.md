# UI evaluation (workstream 4)

- `ASSESSMENT.md`: the three options with flash, CORS, security and maintenance facts, and the recommendation.
- `prototype/index.html`: the proposed device page. One file, no build step, no network dependencies.

Open the prototype:
- **Mock (now):** open the file in a browser or add `?mock`. An unreachable device also ends in mock after 5 s.
- **Against the device (in the LAN):** open the file from disk or over plain HTTP, tap the connection pill and
  enter the address (default `http://192.168.0.201`, stored in the browser). Not from an HTTPS page (mixed content).

Known limits:
- Not tested on hardware. The live path was tested only against a local fake of the 2025.4 REST/SSE API.
- Preset select, volumes, function switches, "Restart Grow" and the status text are `planned: true` in the entity
  table at the top of the script: they work in mock mode and stay disabled on today's firmware.
- pH/EC targets are not published by the firmware yet, so live mode shows a fixed 5.5 to 6.5 pH band and no EC target.
- Emergency stop has no confirm dialog on purpose. Phase names and the Anzucht/Mutter numbers in the mock are made up.
