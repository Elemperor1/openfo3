---
name: openfo3-playtest
description: Launch, inspect, and lightly drive the local OpenFO3 prototype in /Users/jacobcyber/Documents/openfo3. Use when Codex needs to playtest this repo's Fallout 3 runtime, verify startup after engine or content-loading changes, capture screenshots and logs from a live session, send basic macOS key input to the running prototype, or stop and clean isolated OpenFO3 test sessions.
---

# OpenFO3 Playtest

Use the helper script instead of reconstructing `openfo3` arguments by hand. It discovers the build tree, the local Fallout 3 data directory, the selected content files, the fallback archives, and an isolated runtime home under the repo so test runs do not mutate the user's normal OpenMW state.

## Quick Start

1. Run `python3 .codex/skills/openfo3-playtest/scripts/openfo3_playtest.py discover` to verify the inferred binary, resources path, data dir, content files, and archives.
2. Run `python3 .codex/skills/openfo3-playtest/scripts/openfo3_playtest.py launch --no-grab --wait 5`.
3. Run `python3 .codex/skills/openfo3-playtest/scripts/openfo3_playtest.py status --tail 80` and confirm the log contains the startup markers listed in [runtime-notes.md](references/runtime-notes.md).
4. On macOS, run `python3 .codex/skills/openfo3-playtest/scripts/openfo3_playtest.py keys tab --screenshot --screenshot-delay 0.2` or `python3 .codex/skills/openfo3-playtest/scripts/openfo3_playtest.py keys l --screenshot --screenshot-delay 0.1` to exercise panel-safe paths and capture proof in one focused pass.
5. Run `python3 .codex/skills/openfo3-playtest/scripts/openfo3_playtest.py screenshot` when you need a standalone visual artifact from the tracked session.
6. Run `python3 .codex/skills/openfo3-playtest/scripts/openfo3_playtest.py stop` when done.

## Workflow

### Prefer the direct runtime path
- Prefer the direct `openfo3` binary over `openmw-launcher` unless the task is specifically about launcher behavior.
- Prefer the helper script's default build selection unless the user names a different build tree.
- Prefer the repo-local `game_files/Data` install unless the user points at a different Fallout 3 data directory.

### Run a startup smoke test
- Launch with `--no-grab` unless the task explicitly needs raw mouse capture.
- Read `status` output before assuming the runtime is usable.
- Confirm the log shows the startup markers from [runtime-notes.md](references/runtime-notes.md).
- Prefer `keys ... --screenshot` when the screenshot must reflect a just-opened panel or menu state.
- Prefer `--screenshot-delay` over `wait=` tokens for single-key panel transitions so the helper can refocus immediately before capture.
- Use `screenshot` for standalone captures after the helper has refocused the tracked runtime.

### Run a minimal interaction pass
- Focus the running process before sending keys. The `keys` command does this automatically on macOS through `osascript`.
- Prefer short, discrete key sequences over long movement assumptions. The helper sends taps, not raw HID holds.
- Prefer UI-safe actions first: `tab`, `i`, `e`, `w`, `s`, `enter`, `esc`.
- In the level-up panel, `L` toggles between the skills and perks stages.
- State the blocker plainly if macOS Accessibility or Screen Recording permission blocks automation.

### Inspect artifacts and clean up
- Use `status` to retrieve the active PID, current log path, and latest log tail.
- Expect artifacts under `.codex/openfo3-playtest/`:
  - `state.json`
  - `logs/`
  - `screenshots/`
  - `runtime-home/`
- Run `stop` before relaunching so stale state does not hide failures.

## Commands

- `discover`
- `command`
- `launch [--build-dir PATH] [--data-dir PATH] [--no-grab]`
- `status [--tail N]`
- `keys KEY [KEY ...] [--screenshot] [--screenshot-delay SEC] [--screenshot-output PATH]`
- `screenshot [--focus-delay SEC] [--delay SEC]`
- `stop`

Use `keys` tokens such as `w`, `e`, `i`, `tab`, `enter`, `esc`, `space`, `ctrl`, `up`, `down`, `wait=1.0`, or `w*5`.

## When To Override Defaults

- Override `--build-dir` only when the default binary is missing or the user wants a specific build tree.
- Override `--data-dir` only when the repo-local `game_files/Data` is not the intended install.
- Re-run `discover` after build-system or content-loading changes instead of trusting old state.

## References

- Read [runtime-notes.md](references/runtime-notes.md) when you need the control map, runtime-home layout, or expected startup markers.
