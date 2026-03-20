# OpenFO3 Gameplay Roadmap

## Summary
This roadmap orders Fallout 3 gameplay work by the dependencies visible in this repo. The intent is to keep each milestone small enough to validate independently while preserving existing OpenMW behavior.

## Milestones
1. Foundation: finish FO3 object classification and runtime shape so `NPC_`, `WEAP`, `ARMO`, `CONT`, `DOOR`, `TERM`, `MISC`, and world references are stable across `components/esm4`, `apps/openmw/mwclass`, `apps/openmw/mwrender`, and `apps/openmw/mwworld`.
2. Player model: add FO3 SPECIAL, skills, perks, and level progression in `apps/openmw/mwmechanics`, then expose them through `apps/openmw/mwgui`, `apps/openmw/mwlua`, and save-state hooks.
3. Interaction layer: wire basic dialogue, quests, terminals, Pip-Boy, and item activation on top of the player model and object classification, with FO3-specific UI in `apps/openmw/mwgui` and narrative state in `apps/openmw/mwdialogue`.
4. Combat layer: extend weapons, projectiles, ammo, reload behavior, and combat AI in `apps/openmw/mwmechanics`, `apps/openmw/mwworld`, `apps/openmw/mwphysics`, and `apps/openmw/mwsound`.
5. Scripting layer: decide how FO3 `SCPT` content is represented, then bridge quest, terminal, combat, and state changes through the existing Lua and engine-event surface instead of assuming Morrowind script semantics.
6. Persistence: once the gameplay state is real, extend `apps/openmw/mwstate` and per-manager serialization so stats, perks, quests, terminals, combat state, inventory, and scripts survive save/load.
7. Audio and UI completion: refine worldspace, combat, and terminal audio selection in `apps/openmw/mwsound`, then finish any UI gaps in `apps/openmw/mwgui` and `apps/openmw/mwlua` that block a playable loop.

## Assumptions
- World boot and FO3 parsing/archive support land before the gameplay milestones.
- Terminal work starts with a read-only slice before branching, hacking, or full Pip-Boy semantics.
- The first playable target is a minimal exterior-cell loop, not full quest or faction parity.
