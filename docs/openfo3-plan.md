# OpenFO3 Plan

## Boot-To-World Milestone
- Keep `BUILD_OPENFO3` off by default until the slice is stable.
- Build a separate `openfo3` executable under `apps/openfo3` without mutating `apps/openmw` into a Fallout 3 runtime.
- Reuse shared `components` services for files, VFS, resource loading, terrain, and archive parsing.
- Treat this as a build-tree prototype only. Packaging, branding assets, and config namespace splits stay deferred.

## Ownership
- Integration owner:
  top-level [`/Users/jacobcyber/Documents/openfo3/CMakeLists.txt`](/Users/jacobcyber/Documents/openfo3/CMakeLists.txt) and this plan document.
- Runtime owner:
  `apps/openfo3/**`
- Formats and archives owner:
  `components/esm4/**`, `components/esm/**`, `components/bsa/**`
- Launcher owner:
  `apps/launcher/**`, `components/contentselector/**`
- FO3 NPC/model owner:
  `apps/openmw/mwclass/esm4*`, `apps/openmw/mwrender/esm4*`

## Merge Order
1. Integration/docs
2. `apps/openfo3` skeleton
3. FO3 parser and archive fixes
4. Launcher FO3 mode
5. World boot in `apps/openfo3`
6. Movement and collision in `apps/openfo3`
7. FO3 NPC/model safety fixes
8. Basic interaction in `apps/openfo3`

## Current Slice
- Top-level `BUILD_OPENFO3` wiring.
- `apps/openfo3` executable with a direct FO3 bootstrap path.
- FO3-focused parser fixes where `0.94` needs `hasFormVersion()` to avoid TES5 misclassification.
- Launcher FO3 mode and FO3-safe NPC/model runtime guards.

## Blockers
- Full FO3 object semantics still need dedicated systems for inventory, quests, UI, audio, combat, and save/load.
- Terrain, statics, and local interaction can boot independently, but actor/world parity still depends on deeper runtime work.
- The local build environment may still lack Qt6 for launcher builds, so `BUILD_OPENFO3` validation should use a non-Qt configure when necessary.
