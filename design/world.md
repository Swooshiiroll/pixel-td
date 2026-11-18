# World layout and map generator

Status: **confirmed by the owner** (PR #4, 2026-11-18). The decisions from the generator's
planning round the same day are folded in (issue #5): fog as explored ground, sight always direct,
one zoom step in, and the developer tools.

Numbers marked **(start)** are starting values. They live in the live-tuning data file and get
adjusted by feel in the test arena; the owner chose this over settling them on paper.

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
| Screen at default zoom | about 640 x 360 px (about 53 x 30 cells); see Camera |
| Light tower | 2 x 2 cells (24 px) |
| Heavy tower | 4 x 4 cells (48 px) |
| Core | 6 x 6 cells (72 px) |
| Core zone | 26 x 15 cells (312 x 180 px), about a quarter of a screen **(start)** |

- There's one map per run, and it's fixed for the whole run.
- The view is flat top-down.
- The setting is a planet-surface outpost.

### Terrain

Every cell is one of these:

| Terrain | Enemies walk on it? | Build on it? | Line of sight |
|---|---|---|---|
| Open ground (low) | Only where it's path | Yes | Doesn't block |
| High ground (plateau) | Only on the core's ramp (see Path rules) | Yes | Doesn't block |
| Rocks (low blocker) | No | No | Block direct fire from towers on low ground |
| Cliffs / mountains (tall blocker) | No | No | Block every shot |
| Path | Yes | No | Doesn't block |

**High ground:**
- A tower standing on it gets a range bonus: +25% **(start)**.
- It can fire over rocks, but not over cliffs or mountains.

**Line of sight:**

| Shooter | Rocks | Cliffs / mountains |
|---|---|---|
| Direct fire from low ground | Blocked | Blocked |
| Direct fire from high ground | Fires over | Blocked |
| Lobbed weapon (any height) | Arcs over | Blocked |

- **The check:** a shot is blocked if the straight line from the tower's centre to the enemy's
  centre crosses a cell that blocks that shot.
- Whether a weapon fires directly or is lobbed is a trait of the weapon part
  ([towers.md](towers.md)).

There's no terrain that affects enemies and no resource deposits.

### How a map is made (run start)

1. **Stage 1, from the seed:** the generator makes the terrain and **one core zone**.
   - The zone can be anywhere on the map, and it can contain any terrain: open ground, high
     ground, rocks and cliffs.
   - Before the map is accepted, every legal core spot in the zone is checked so that it can
     route to a map edge with a fair path (see Edge cases).
2. **Core placement:**
   - You see the terrain and the highlighted zone, and you place the 6 x 6 core anywhere inside
     it.
   - The core must sit on buildable cells, either all low ground or all high ground.
   - This is **blind**: the spawn is hidden and no path exists yet. You choose by reading the land.
   - You can move the core around until you confirm it.
3. **Stage 2, from the seed and the core spot:**
   - The generator picks **the spawn on a map edge**, far enough from the core for a fair path.
   - It then routes **the path** from the spawn to the core.
4. **Fog** covers the path beyond the core's surroundings (see Visibility), and the first build
   phase begins.

**Determinism:**
- The whole map is a function of (seed, core spot): the same pair always gives the same map.
- Generation uses only `pixeltd::Rng`, in `src/core`.

### Path rules

- **v0.1 has one spawn and one path** to the core.
- **Low ground only.** The path never crosses high ground, rocks or cliffs.
  - **The one exception:** if the core is on high ground, the path's last stretch climbs a
    generated ramp up to it. That's all a high-ground core does: it changes nothing else.
  - The ramp stays within 6 cells of the core **(start)**, so it's always inside the area explored
    when you confirm the core.
- **Shape varies by seed:** some maps wind with switchbacks and others run fairly direct, always
  within a fair length range of 1.5 to 2.5 times the map's width, measured along the path
  **(start)**.
- **Width:** the seed picks a **base width for the map**, then adds **chokepoints** (narrower)
  and **wide stretches** along the path.
  - Base width 2 to 4 cells; chokepoints down to 2 cells; wide stretches up to 6 cells **(start)**.
- **Later in development (v0.2 and on), not v0.1:**
  - several spawns whose paths merge;
  - forks that rejoin;
  - late spawns that sit dormant and open at set waves.
  - The owner's rule: start simple, and later maps get more complicated.

### Building

- Towers go on **any open ground or high ground** that isn't path, blocker or core.
- **A footprint can't straddle heights.** It sits entirely on low ground or entirely on high
  ground.

### Visibility (fog)

Fog works as **explored ground**, as the owner chose on 2026-11-18.

- **The terrain is always visible.** You can always see where the ground, plateaus, rocks and
  cliffs are.
- **Unexplored low ground has a light haze.** Path under the haze is drawn as plain ground, so you
  can see where you haven't looked, but not where the path runs there.
  - Only low ground is hazed, because the path never runs on high ground except the ramp, and the
    ramp is always explored from the start.
- **Exploring:** every cell in sight gets explored, not only path.
  - **When you confirm the core,** every cell within one light tower's range of the core is
    explored. Until towers define their range, that's 6 cells, measured from the nearest core cell
    **(start)**.
  - **After that,** every cell inside a tower's range that the tower can see is explored.
  - **Sight is always direct line of sight for the tower's height,** even for lobbed weapons. A
    lobbed tower behind rocks can hit what it can't see, but only once another tower, or high
    ground, has explored it for it: artillery needs a spotter.
  - Enemies walking a stretch **don't** explore it.
- **Explored for good:** once a cell is explored it stays explored, even if the tower that saw it
  is sold.
- **Enemies are hidden on unexplored cells.** You see an enemy once it walks onto explored ground.

### Camera

- **Pan:** WASD, the screen edges, or a mouse drag.
- **Zoom:** in whole-number scale steps, so pixels stay crisp.
  - At every step the view **fills the window**, with no black bars.
  - **The default zoom** is the largest whole-number scale whose view is at least 640 x 360. For
    example, 3x on 1920x1080 gives exactly 640x360, and 2x on 1366x768 gives 683x384.
  - **Zooming out** stops at the last step whose view still fits inside the map. Nothing outside
    the map is ever shown. On 1080p that's 2x, which shows 960x540.
  - **Zooming in** goes one step closer than the default: 4x on 1080p, which shows 480x270.
  - For very wide or tall windows, the default steps in until its view fits inside the map.
  - **Controls:** the mouse wheel zooms about the cursor, and + and - zoom about the centre of the
    screen.

### Developer tools

These exist for building and reviewing the generator. **Normal runs stay random-only**: these
are launch flags and keys for development, not settings in the game.

- `--seed N` generates the map from seed N (decimal, or hex with `0x`).
- **F5** makes a new map from a new random seed.
- The **HUD** shows the seed, the zoom, the generation time, and the cell under the cursor.
- `--map-shot file.bmp` saves the whole 1920x720 map as an image, then exits.
- `--core auto` (with the core placement work) places the core automatically, for smoke runs and
  map shots.

The test arena will get proper seed entry later.

## Interactions

- **Towers ([towers.md](towers.md)):**
  - Footprints use this grid.
  - High ground and line of sight affect range and targeting.
  - Whether a weapon is lobbed is a trait of the part, but sight is always direct.
  - Towers explore fog within their range and sight.
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
  - The ground around the core is explored, so the nearby path appears. Unexplored low ground
    elsewhere stays under the haze.
  - The camera starts centred on the core.
- **Range preview:**
  - While you place a tower, its range circle includes the high-ground bonus.
  - Cells it can't see because of line of sight are shaded.

## Edge cases

- **The zone has no legal spot, or a spot can't route.** Stage 1 rejects that map and regenerates
  from a sub-seed derived from the seed (still deterministic), so a player never sees a broken
  zone.
  - It gives up after 64 attempts and uses a known-safe fallback layout.
  - A test asserts the fallback is never needed across 1,000 seeds.
- **Stage 2 routing fails anyway:** it retries with derived sub-seeds, then falls back to the
  shortest legal route. A run must never be unwinnable from its first frame.
- **Build space:** every 10-cell stretch of path has at least one legal light-tower spot within
  light-tower range.
- **Windows smaller than 640 x 360:** 1x, showing what fits.
- **Placing the core over illegal cells,** such as blockers or a footprint across two heights: the
  ghost shows red and you can't confirm.

## Scope

- **v0.1:**
  - stage 1 and stage 2;
  - one spawn to the core;
  - rocks, cliffs and high ground;
  - core placement, including on high ground with the ramp;
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
- **Sweep over 1,000 seeds.** For each map:
  - every legal core spot routes;
  - the path connects the spawn to the core;
  - the path stays on low ground, except the ramp to a high-ground core;
  - the path never touches blockers;
  - widths and length stay within their ranges;
  - the spawn is on a map edge;
  - the build-space guarantee holds;
  - the fallback is never used.
- **Line of sight:** one test for each row of the line-of-sight table, using the centre-to-centre
  check.
- **Footprints:**
  - legal and illegal placements (path, blocker, core, straddling heights, off-map);
  - the high-ground bonus only on high ground.
- **Fog:**
  - exploring around the core at confirm, which covers the ramp;
  - exploring by a tower in range with direct sight, including a lobbed tower that can't see past
    rocks;
  - no exploring by an enemy walking;
  - explored cells stay explored after a sale;
  - enemies on unexplored cells are not visible.
- **Camera:** the default, zoom-out and zoom-in steps for 1920x1080, 1366x768 and 2560x1440, plus
  small and very wide windows.
- **Performance:** measure the time per stage.
  - **Proposed** target: under 50 ms per stage in a Release build on ANODE-MGMT.

## Open questions

1. The performance target for generation (Proposed: under 50 ms per stage).
2. For the arena or tower round: placing a tower on unexplored ground that hides path would be
   refused, which gives away that the path runs there. Should building on unexplored ground be
   allowed, refused, or allowed with that leak accepted?

Everything else from the first draft was settled with the owner on 2026-11-18. The remaining
numbers are **(start)** values to tune in the arena.
