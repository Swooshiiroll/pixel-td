# World layout and map generator

Status: **draft for the owner's review** (issue #3). Decided in the question rounds of 2026-11-18.
Anything marked **Proposed** wasn't decided: it's a starting value or rule for the owner to
confirm, and it's listed under Open questions.

## Purpose

Every run takes place on a fresh map that is generated from a random seed. The map should make you
read the land: you place the core by judging the terrain, then you learn the route the enemies
take and defend it. That read of the land is the main decision in a run.

## Rules and numbers

### Map and grid

| What | Size |
|---|---|
| Cell (placement grid) | 12 x 12 px |
| Map | 1920 x 720 px = **160 x 60 cells** (about 3 x 2 screens) |
| Screen at default zoom | 640 x 360 px (about 53 x 30 cells) |
| Light tower | 2 x 2 cells (24 px) |
| Heavy tower | 4 x 4 cells (48 px) |
| Core | 6 x 6 cells (72 px) |
| Core zone | about a quarter of a screen. **Proposed:** 26 x 15 cells (312 x 180 px) |

- There's one map per run, and it's fixed for the whole run.
- The view is flat top-down.
- The setting is a planet-surface outpost.

### Terrain

Every cell is one of these:

| Terrain | Enemies walk on it? | Build on it? | Line of sight |
|---|---|---|---|
| Open ground (low) | Only where it's path | Yes | Doesn't block |
| High ground (plateau) | Never | Yes | Doesn't block |
| Rocks (low blocker) | No | No | Block direct fire from towers on low ground |
| Cliffs / mountains (tall blocker) | No | No | Block every shot |
| Path | Yes | No | Doesn't block |

**High ground:**
- A tower standing on it gets a range bonus (**Proposed:** +25%).
- It can fire over rocks, but not over cliffs or mountains.

**Line of sight:**

| Shooter | Rocks | Cliffs / mountains |
|---|---|---|
| Direct fire from low ground | Blocked | Blocked |
| Direct fire from high ground | Fires over | Blocked |
| Lobbed weapon (any height) | Arcs over | Blocked |

- **Proposed:** a shot is blocked if the straight line from the tower's centre to the target's
  centre crosses a blocking cell.
- Whether a weapon fires directly or is lobbed is a trait of the weapon part
  ([towers.md](towers.md)).

There's no terrain that affects enemies and no resource deposits.

### How a map is made (run start)

1. **Stage 1, from the seed:** the generator makes the terrain and **one core zone**. The zone can
   be anywhere on the map. Before the map is accepted, every legal core spot in the zone is checked
   so that it can route to a map edge with a fair path (see Edge cases).
2. **Core placement:**
   - You see the terrain and the highlighted zone, and you place the 6 x 6 core anywhere inside
     it on buildable ground.
   - This is **blind**: the spawn is hidden and no path exists yet. You choose by reading the land
     (high ground, rocks and cliffs nearby).
   - **Proposed:** you can move the core freely until you confirm it.
3. **Stage 2, from the seed and the core spot:**
   - The generator picks **the spawn on a map edge**, far enough from the core for a fair path.
   - It then routes **the path** from the spawn to the core.
4. **Fog** covers the far part of the path (see Visibility), and the first build phase begins.

**Determinism:**
- The whole map is a function of (seed, core spot): the same pair always gives the same map.
- Generation uses only `pixeltd::Rng`, in `src/core`.

### Path rules

- **v0.1 has one spawn and one path** to the core.
- **Low ground only.** The path never crosses high ground, rocks or cliffs.
- **Shape varies by seed:** some maps wind with switchbacks and others run fairly direct, always
  within a fair length range.
  - **Proposed** length range: 1.5 to 2.5 times the map's width in cells, measured along the
    path.
- **Width:** the seed picks a **base width for the map**, then adds **chokepoints** (narrower)
  and **wide stretches** along the path.
  - **Proposed:** base width 2 to 4 cells; chokepoints down to 2 cells; wide stretches up to
    6 cells.
- **Later in development (v0.2 and on), not v0.1:**
  - several spawns whose paths merge;
  - forks that rejoin;
  - late spawns that sit dormant and open at set waves.
  - The owner's rule: start simple, and later maps get more complicated.

### Building

- Towers go on **any open ground or high ground** that isn't path, blocker or core.
- **Proposed:** a tower's footprint can't straddle low ground and high ground. It must sit
  entirely on one level, and it only gets the high-ground bonus if all of it is up there.

### Visibility (fog)

- The terrain is always visible.
- After you confirm the core, the path near the core is visible. The rest of the path and the
  spawn edge are **fogged until scouted**: the first enemies walking a stretch reveal it, and so
  does a tower that can see it.
- **Proposed:**
  - At the start, the core reveals the path within a radius of a light tower's range.
  - After that, any path cell inside a tower's range (with line of sight) is revealed.
  - Every path cell an enemy has walked is revealed.
  - Once a cell is revealed, it stays revealed.

### Camera

- **Pan:** WASD, the screen edges, or a mouse drag.
- **Zoom:** in whole-number scale steps, so pixels stay crisp. For example, on a 1920x1080 window,
  3x shows 640x360 (the default), 2x shows 960x540, and 1x shows 1920x1080.
- **Proposed:** zooming out stops at the last step whose view still fits inside the map's
  height. On 1080p that's 2x.

## Interactions

- **Towers ([towers.md](towers.md)):**
  - Footprints use this grid.
  - High ground and line of sight affect range and targeting.
  - Whether a weapon is lobbed is a trait of the part.
- **Test arena:** uses this generator, with a seed you type in. It's the first thing built on top
  of it.
- **Saved data:** none in v0.1. There's no save or load yet. A map is fully rebuilt from (seed,
  core spot).

## UI

- **Core placement:**
  - The terrain is shown with the zone highlighted.
  - A ghost of the core snaps to the 12 px grid and is coloured by whether that spot is legal.
  - A confirm control starts stage 2.
- **After confirming:**
  - The path appears near the core, and the rest is under fog.
  - The camera starts centred on the core.
- **Range preview:** while you place a tower, its range circle includes the high-ground bonus.
  - **Proposed:** cells blocked by line of sight are shaded.

## Edge cases

- **The zone has no legal spot, or a spot can't route.** Stage 1 rejects that map and regenerates
  from a sub-seed derived from the seed (still deterministic), so a player never sees a broken
  zone.
  - **Proposed:** a cap of 64 attempts, then a known-safe fallback layout. A test asserts the
    fallback is never needed across many seeds.
- **Stage 2 routing fails anyway:** it retries with derived sub-seeds, then falls back to the
  shortest legal route. A run must never be unwinnable from its first frame.
- **Build space:** every stretch of path needs buildable cells beside it.
  - **Proposed:** at least one legal light-tower spot within light-tower range of every
    10-cell stretch.
- **The window isn't a multiple of 640x360:** the default zoom is the largest whole-number scale
  that shows at least 640x360 (see Open questions).
- **Placing the core over terrain that isn't buildable:** the ghost shows red and you can't
  confirm.

## Scope

- **v0.1:**
  - stage 1 and stage 2;
  - one spawn to the core;
  - rocks, cliffs and high ground;
  - core placement;
  - fog;
  - the camera.
- **Later:**
  - several spawns, forks and late spawns (v0.2+);
  - art for the terrain (the style board first).
- **Out:** terrain that affects enemies, resource deposits and typed seeds in normal runs. Normal
  runs are random only.

## Acceptance (what the tests check)

All in `src/core`, headless, in doctest:

- **Determinism:**
  - The same seed gives an identical terrain and zone.
  - The same (seed, core spot) gives an identical spawn and path.
- **Sweep over many seeds** (**Proposed:** 1,000). For each map:
  - every legal core spot routes;
  - the path connects the spawn to the core;
  - the path is only on low ground and never touches blockers or high ground;
  - widths and length stay within their ranges;
  - the spawn is on a map edge;
  - the fallback is never used.
- **Line of sight:** one test for each row of the line-of-sight table.
- **Footprints:**
  - legal and illegal placements (path, blocker, core, straddling heights, off-map);
  - the high-ground bonus only when the whole footprint is up there.
- **Fog:**
  - the initial reveal;
  - reveal by a tower;
  - reveal by an enemy walking;
  - revealed cells stay revealed.
- **Performance:** measure the time per stage.
  - **Proposed** target: under 50 ms per stage in a Release build on ANODE-MGMT.

## Open questions

1. The core zone's exact size (Proposed 26 x 15 cells).
2. The high-ground range bonus (Proposed +25%).
3. The line-of-sight rule (Proposed: centre to centre, crossing a blocking cell).
4. Path length range and widths (Proposed above).
5. Footprints straddling heights (Proposed: not allowed).
6. Fog reveal radius and rules (Proposed above).
7. Zoom-out limit, and how windows that aren't a multiple of 640x360 are handled: letterbox, or
   fill the window with a slightly larger view?
8. Can the core sit on high ground, and does that do anything?
9. Can the core zone contain rocks or cliffs, or is it always clear ground?
10. The generator's fallback cap and the build-space guarantee (Proposed above).
