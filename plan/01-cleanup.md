# 01 Cleanup

## Intent
A tree where every file earns its place, and docs that match the code. This lowers the token
cost of every later step.

## Decided with the owner
- Deleting and archiving is **allowed**. Git history is the backup. Dead components, ballast
  files and stale docs can go. Whatever still has value as background can move to a short
  `docs/archive/` (or be condensed) instead of being kept verbatim.
- The end state is **`main` only**. `BLE-Shelly`: check whether anything in it is still useful
  (BLE Shelly control, the `huge_app.csv` partition). Carry over what is worth it and record the decision in `LOG.md`.
  Before deleting the remote branch, set a tag (e.g. `archive/BLE-Shelly`) so nothing is lost.
- `TODO.md` is the owner's file. Reorganize it rather than delete it (or ask in `QUESTIONS.md`).
- Keep `flake.nix` and `Hardware/`.

## Done when
- No unreferenced components or files, and the build is still green.
- `AGENTS.md`, `MAP.md` and the remaining docs agree with the code.
- `git branch -a` shows only `main` (+ HEAD).
