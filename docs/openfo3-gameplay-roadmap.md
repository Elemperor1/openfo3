# OpenFO3 Gameplay Roadmap

## Summary
This is the execution-order document for Fallout 3 gameplay work in `openfo3`. Use it to decide what gets built next and in what order.

Use `../checklist.txt` as the exhaustive parity ledger. The checklist is intentionally broader and flatter than this roadmap.

The roadmap optimizes for a believable playable Fallout 3 slice first, then broad base-game completeness, then DLC/mod/parity polish.

## Priority Matrix
- `P0`: blocks a believable playable Fallout 3 slice
- `P1`: needed for a recognizably complete base-game experience
- `P2`: needed for broad vanilla/DLC parity
- `P3`: polish, platform, or compatibility extras

## Current Prototype Slice
The current `openfo3` prototype already covers:
- FO3 data/bootstrap path, direct `openfo3` runtime boot, and BSA-backed asset loading
- Exterior and interior loading, including door travel and same-cell interior teleports
- Terrain/object streaming with local terrain plus near/far/landmark object bands
- Settlement-biased startup spawning instead of empty wasteland bootstrap cells
- Grounded movement/collision prototype across exteriors and interiors
- Pickups, containers, session-local inventory, and readable books/notes
- Prototype lock/hack gating with fixed skills and session-local unlock state

Still explicitly missing from the current slice:
- real player stats and progression
- barter/repair/crafting loops
- real-time combat and V.A.T.S.
- dialogue, quests, Pip-Boy, map flow, and fast travel
- companions, crime/karma, and broader world simulation
- save/load parity, DLC completeness, and representative mod compatibility

## Phase 1: Playable Core RPG Loop
Owns the systems that turn the current exploration sandbox into a minimal Fallout 3 RPG loop.

- `P0` Player state shell: SPECIAL, the 13 FO3 skills, derived stats, XP gain, level-up flow, perk selection shell, bobbleheads, and skill-book effects.
- `P0` Core item/service loops: real inventory semantics, condition, weight/encumbrance, aid effects, radiation baseline, addictions baseline, wait/sleep, and basic death/healing/fall-damage rules.
- `P0` Access mechanics: replace the current prototype lock/hack gating with real lockpicking and hacking minigames, including key bypass behavior.
- `P0` Economic loops: barter, repair services, vendor inventories/caps, workbench activation, schematics, and crafted-item output rules.
- `P1` UI support needed for the above: loot, barter, repair, workbench, level-up, wait/sleep, and status/effects surfaces. Use the current prototype UI where practical; do not block this phase on full FO3 menu parity.

Assumes:
- The current prototype slice remains stable.
- Save/load parity is still deferred.

Exit criterion:
- The player can explore, loot, heal, level, lockpick/hack, trade, repair, and craft in a stable loop without requiring combat, quests, or full Pip-Boy parity first.

## Phase 2: Combat, Damage, and Survival
Owns the moment-to-moment combat and survivability model.

- `P0` Real-time combat loop with weapon classes, ammo, reloads, spread/recoil, hit logic, and condition degradation.
- `P0` Damage model: damage formulas, crits, limb damage/crippling, knockdown/stagger, explosives, mines/traps, armor/clothing condition, and resistance handling.
- `P0` Equipment/combat inventory semantics: equip slots, equip conflicts, hotkeys/favorites behavior, apparel bonuses, ammo consumption, and over-encumbered movement penalties.
- `P1` V.A.T.S.: active-pause targeting, body-part targeting, AP costs, queued actions, and killcam behavior.
- `P1` Survival/hazard expansion tied to combat spaces: environmental hazards, drowning/swimming hazards where relevant, and stronger chem/consumable interactions.

Assumes:
- Phase 1 player stats, inventory, repair, and service loops exist.
- Prototype locomotion/collision remains the traversal baseline.

Exit criterion:
- Common FO3 combat encounters are playable with correct-feeling weapons, damage, armor, consumables, and V.A.T.S., even if quest and dialogue parity are still incomplete.

## Phase 3: Dialogue, Quests, Pip-Boy, and Map Flow
Owns the narrative/UI layer that makes Fallout 3 progression and world navigation coherent.

- `P0` Dialogue system: topic/response trees, checks, greetings/barks, service menus, subtitles, and voiced timing hooks.
- `P0` Quest flow: stages, objectives, journal updates, target markers, scripted objective progression, fail/success branches, holotape/terminal-driven quest hooks, and branching-end scaffolding.
- `P0` Pip-Boy and menu stack: items/data/radio tabs, quest/note/stat/effect pages, pause/save/load/help surfaces, local/world map views, and loading/help popups.
- `P0` World navigation: map markers, discovered state, fast travel, compass/objective markers, local map display, and radio/quest marker flow.
- `P1` Terminal/UI fidelity work that belongs with narrative flow, including FO3-style terminal presentation and the broader move toward regular FO3 GUI behavior.

Assumes:
- Phase 1 and Phase 2 already provide stable stats, items, combat, and service interactions.
- Read-only terminals from the prototype can be replaced or expanded here.

Exit criterion:
- Main-quest and side-quest structure can be authored and played through a recognizably Fallout 3-style UI/navigation flow, even if world-simulation edge cases and DLC completeness are still pending.

## Phase 4: World Simulation, Companions, and Crime/Karma
Owns the systemic open-world behavior that makes settlements, followers, stealth, and faction reaction feel like Fallout 3 rather than a scripted demo.

- `P1` NPC and creature simulation: schedules, sandboxing, patrols, combat AI, fleeing/searching, sleeping/eating/use-item packages, leveled actors, and leveled loot.
- `P1` Pathfinding/navmesh parity close enough for packages, escorts, combat pursuit, and settlement traversal to behave correctly.
- `P1` Companions: recruitment rules, party limits, Dogmeat special slot handling, commands, death/essential-state behavior, karma-gated recruitment, and DLC rule changes where needed.
- `P1` Detection/stealth: line of sight, hearing, alert states, sneak attacks, stealth multipliers, and faction awareness.
- `P1` Crime/karma/faction response: ownership, stealing, trespass, witnesses, localized crime knowledge, karma thresholds, guards/arrest logic, and world reaction changes.
- `P2` Open-world systemic behavior: random encounters, scripted encounter regions, unmarked locations, settlement safety/storage expectations, persistent references, respawn/regeneration, and broader faction-state flips.

Assumes:
- Dialogue, quests, combat, and inventory/service loops are already functioning.
- Map/travel flow from Phase 3 exists.

Exit criterion:
- Towns, followers, guards, stealth spaces, and random world encounters behave plausibly enough that Fallout 3 feels like a living RPG world rather than a chain of isolated scripted interactions.

## Phase 5: Persistence, DLC, Mod Compatibility, and Parity Polish
Owns the long-tail work needed for broad vanilla/DLC completeness and representative compatibility.

- `P0` Save/load parity for the systems added in earlier phases: stats, perks, quests, terminals, combat state, inventory, containers, unlock state, and script-driven world changes.
- `P1` FO3 script command coverage and quest/package/dialogue command behavior sufficient for vanilla plus DLC content to run without routine softlocks.
- `P1` Base-game completion pass: quests, misc objectives, world-state changes, radio stations, companions, player homes, bobbleheads, skill books, and Capital Wasteland content parity.
- `P2` DLC completeness: Operation: Anchorage, The Pitt, Broken Steel, Point Lookout, and Mothership Zeta, including post-ending Broken Steel continuation.
- `P2` Representative mod compatibility: plugin loading/conflict behavior, loose-file override behavior, dirty-data tolerance, archive invalidation-equivalent behavior, INI/settings expectations, and a FOSE-less compatibility bar.
- `P2` Audio/video/facial parity: radio behavior, exploration/combat music flow, lipsync, FaceGen/facial animation, Bink/video support where needed, and loading-screen/tip completeness.
- `P3` Remaining UI/render/platform polish: broader FO3 GUI fidelity, edge-case menu behavior, achievements/trophies if desired, and final parity cleanup for visual/audio subsystems.

Assumes:
- Earlier phases already delivered a stable playable base-game loop.
- DLC and mod work is not allowed to redefine the core runtime shape established earlier unless required for compatibility.

Exit criterion:
- `openfo3` can plausibly target vanilla/GOTY completion plus representative FOSE-less mods without major systemic softlocks, while long-tail polish remains incremental rather than foundational.
