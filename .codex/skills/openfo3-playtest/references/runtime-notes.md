# OpenFO3 Runtime Notes

## Launch contract

- `openfo3` requires `--resources`, at least one `--data`, and at least one `--content`.
- Prefer the app bundle's `Contents/Resources/resources` directory as the `--resources` root.
- Fall back to `Contents/Resources` only when the nested `resources/` directory is absent.
- Use the directory that contains `Fallout3.esm` as the `--data` root. In this repo the default is `<repo>/game_files/Data`.
- Pass content names, not absolute content paths. Use `Fallout3.esm` first, then append entries from `DLCList.txt` that exist on disk.
- Pass fallback archive names, not absolute archive paths. Use the base BSAs first, then append `<DLC> - Main.bsa` and `<DLC> - Sounds.bsa` for each selected DLC when present.

## Isolated runtime home

- Mirror the launcher's OpenFO3 runtime isolation instead of writing into the user's normal OpenMW config tree.
- On macOS, create:
  - `Library/Preferences/openmw`
  - `Library/Application Support/openmw`
  - `Library/Caches/openmw`
- Write a stub `openmw.cfg` into `Library/Preferences/openmw/openmw.cfg`.
- Set `HOME` to the isolated runtime-home directory before launch.

## Controls

### World

- Move: `W`, `A`, `S`, `D`
- Move up/down: `Space`, `Ctrl`
- Sprint: `Shift`
- Interact: `E`
- Inventory: `I`
- Status: `Tab`
- Toggle collision: `C`
- Quit: `Esc`

### Panel

- Close: `Tab` or `Esc`
- Select previous/next: `W` or `Up`, `S` or `Down`
- Confirm: `E` or `Enter`
- Container take-all: `A`
- Read selected inventory/container entry: `R`
- Status debug XP: `G`
- Open level-up from status and toggle skills/perks within level-up: `L`

## Expected startup markers

Look for log lines containing:

- `OpenFO3 prototype`
- `Using OpenFO3 resource data directory`
- `OpenFO3 loaded`
- `OpenFO3 initial camera target`

Normal movement can also produce `OpenFO3 updated active grid` lines.

## Limitations

- `openfo3` does not expose `--skip-menu`, `--script-run`, or save-load shortcuts the way `openmw` does.
- With `--no-grab`, the runtime expects RMB-held mouse look. Automated playtests should prefer screenshots and keyboard-safe UI paths unless the user approves richer GUI automation.
