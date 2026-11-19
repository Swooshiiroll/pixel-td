# World layout and map generator

Status: **confirmed by the owner** (PR #4, 2026-11-18), then **reworked by the owner the same day**
(issue #5). The world became a seamless heightmap at 3840 x 2160 px. The seed now places the core
and the spawns, and enemies find their own way instead of following a generated path (see
[pathfinding](#enemies-and-routes)).

Numbers marked **(start)** are starting values. They live in the live-tuning data file and get
adjusted by feel in the test arena; the owner chose this over settling them on paper.

## Purpose

Every run takes place on a fresh map generated from a random seed: hills, valleys, cliff faces
and towering rock formations around a core near the centre. You read the land to decide where to
build. Height gives range and sight, cliffs and formations shape where enemies can walk, and your
towers shape the rest of their route.

## Rules and numbers

### Map and grid

| What | Size |
|---|---|
| Map | **3840 x 2160 px**: four 1920x1080 screens in a 2 x 2 layout |
| Hidden grid | 12 x 12 px cells: **320 x 180 cells** |
| Screen at default zoom | about 640 x 360 px (about 53 x 30 cells), a 36th of the map; see Camera |
| Light tower | 2 x 2 cells (24 px) |
| Heavy tower | 4 x 4 cells (48 px) |
| Core | 6 x 6 cells (72 px) |
| Spawn | 2 x 2 cells (24 px) **(start)** |

- **The look is seamless.** There are no tiles or grid lines; height is shaded smoothly across
  cells.
- **The grid is hidden** but real: towers, the core, spawns and pathfinding all snap to its 12 px
  cells.
- There's one map per run, and it's fixed for the whole run.
- The view is flat top-down, and the setting is a planet-surface outpost.

### Terrain

Every cell has:

| Layer | What it is | Gameplay |
|---|---|---|
| **Height** | A smooth, continuous altitude, 0–200 for ground **(start)** | Range, line of sight, cliffs, building |
| **Material** | The ground type (dust, gravel, ash, rust...) | Looks only, for now |
| **Formation** | Part of a large rock formation, or not | Can't build, can't walk, blocks every shot |

- **Brightness shows height** (brighter is higher), and **hue shows the material**.
- **Hills, valleys and mesas.** The generator layers smooth noise for rolling terrain and
  terraces some regions into mesas with sheer sides.
- **Cliff faces.** A step between neighbouring cells greater than **12 height units (start)** is a
  cliff.
  - Enemies can't cross a cliff, in either direction.
  - A cell beside a cliff can't be built on.
- **Rock formations.** Large outcrops, **8–16 per map**, each **4–20 cells across (start)**. They
  stand far above the surrounding terrain, at height 210–255.
  - Nothing can be built on them.
  - Nothing can walk through them.
  - They block sight and **every** shot, lobbed shots included.

### Line of sight and range

- **Direct fire and sight** need a clear line over the terrain.
  - The line runs from the tower's eye (its ground height **+10 (start)**) to the target
    (its ground height **+4 (start)**).
  - A shot is blocked if the terrain anywhere along the line, or any formation, rises above it.
  - The line is checked centre to centre. Where it passes exactly through a cell corner, both
    cells beside it count.
- **Lobbed weapons** arc over terrain. Only rock formations block them.
- **Sight is always direct**, even for lobbed weapons. A lobbed tower can hit what it can't see,
  but only once something else has explored it: artillery needs a spotter.
- **Range grows with height above the target:**
  - +5% per 10 height units the tower stands above the target, capped at **+50% (start)**;
  - shooting uphill shortens it the same way, down to **-25% (start)**.
  - So a tower's reach isn't a circle: it reaches further over low ground than up a hill.
- Whether a weapon fires directly or is lobbed is a trait of the weapon part
  ([towers.md](towers.md)).

### How a map is made (run start)

All from the seed, at run start. There's no placement step: the run opens straight into the
first build phase.

1. **Terrain:** height, mesas, cliffs, rock formations and materials.
2. **The core:**
   - The seed picks a 6 x 6 spot in the **central third** of the map (cells x 107–212,
     y 60–119).
   - The spot must be buildable (see Building).
3. **Spawns:** the seed places **3**, each on a map edge or inside the map (chosen 50/50
   **(start)**).
   - **Reachable:** each can reach the core without crossing a cliff or a formation.
   - **Far enough:** each is at least **120 steps (start)** from the core by the shortest walk.
   - **Spread out:** spawns are at least **60 cells (start)** apart.
   - **Opening times:** one is active from wave 1, the second opens at wave 10 and the third at
     wave 20 **(start)**.
   - **Hidden** until explored (see Visibility).
4. **Fog** covers everything but the core's surroundings.

**Determinism:**
- The whole map is a function of the seed: the same seed always gives the same map.
- Generation uses only `pixeltd::Rng` and integer maths, in `src/core`. Golden hashes check that
  MSVC and GCC agree.

### Enemies and routes

There's **no generated path**. Enemies find their own way from the spawns to the core, and your
towers shape the route. The details have their own design doc, designed and built with the test
arena (issue #11). Decided so far:

- **Mazing:** towers occupy ground, and enemies route around them.
- **Sealed in:** if your towers cut a spawn off from the core, enemies **break towers** to get
  through. Placements are never refused for blocking.
- **The routing rule is set per enemy type** (for example, runners take the shortest path), as
  part of the enemy design.
- **Gaps:** enemies fit through any 1-cell (12 px) gap.
- **Movement:** cliffs and rock formations block it.
- **Pinned:** whether slopes slow enemies.

### Building

- Towers (and the core) go on **fairly flat ground**: across the footprint, the height range must
  be at most **6 units (start)**, with no cliff cell and no rock formation.
- A tower stands at its ground's height, which sets its range and sight.

### Visibility (fog)

Fog works as **explored ground**:

- **The terrain is always visible:** height, cliffs and formations.
- **Unexplored ground has a light haze.** Spawns and enemies under it are hidden.
- **Exploring:**
  - **At run start,** every cell within one light tower's range of the core is explored. Until
    towers define their range, that's 6 cells from the nearest core cell **(start)**.
  - **After that,** a tower explores every cell it can see within its reach (range adjusted for
    that cell's height).
  - **Sight is always direct line of sight**, even for lobbed weapons.
  - Enemies walking somewhere **don't** explore it.
- **Explored for good:** once a cell is explored it stays explored, even if the tower that saw it
  is sold.
- **Enemies are hidden on unexplored cells.** You see an enemy once it walks onto explored ground.
- **A spawn is hidden** until its cells are explored.

### Camera

- **Pan:** WASD, the arrow keys, the screen edges, or a right-drag.
- **Zoom:** in whole-number scale steps, so pixels stay crisp.
  - At every step the view **fills the window**, with no black bars.
  - **The default zoom** is the largest whole-number scale whose view is at least 640 x 360. For
    example, 3x on 1920x1080 gives exactly 640x360, and 2x on 1366x768 gives 683x384.
  - **Zooming out** stops at the last step whose view still fits inside the map. On 1080p that's
    1x, which shows 1920x1080: a quarter of the map.
  - **Zooming in** goes one step closer than the default: 4x on 1080p, which shows 480x270.
  - **Controls:** the mouse wheel zooms about the cursor, and + and - zoom about the centre of the
    screen.
- The camera starts centred on the core.

### Developer tools

These exist for building and reviewing the generator. **Normal runs stay random-only**: these
are launch flags and keys for development, not settings in the game.

- `--seed N` generates the map from seed N (decimal, or hex with `0x`).
- **F5** makes a new map from a new random seed.
- The **HUD** shows the seed, the zoom, the generation time, and the cell under the cursor with its
  height.
- `--map-shot file.bmp` saves the whole 3840x2160 map as an image (fully explored), then exits.

The test arena will get proper seed entry later.

## Interactions

- **Towers ([towers.md](towers.md)):**
  - Footprints use the hidden grid and need fairly flat ground.
  - Height sets range and sight.
  - Whether a weapon is lobbed is a trait of the part, but sight is always direct.
  - Towers explore fog within their reach and sight.
- **Enemies and pathfinding:** they walk the terrain around cliffs, formations and towers (issue
  #11).
- **Test arena:** uses this generator, with a seed you type in.
- **Saved data:** none in v0.1. A map is fully rebuilt from its seed.

## UI

- **Run start:**
  - The camera is centred on the core.
  - The core's surroundings are explored; everything else is under the haze.
- **Range preview** (while placing a tower): the cells it can reach and see are outlined, and
  cells out of reach or out of sight are shaded.

## Edge cases

- **No buildable core spot in the central third, or spawns can't be placed** (unreachable, too
  close, too crowded): the attempt is rejected and the map regenerates from a sub-seed derived from
  the seed (still deterministic).
  - It gives up after 64 attempts and uses a known-safe fallback layout.
  - A test asserts the fallback is never needed across 1,000 seeds.
- **A spawn walled in by towers:** enemies break towers (pathfinding doc).
- **Windows smaller than 640 x 360:** 1x, showing what fits.

## Scope

- **v0.1:**
  - the heightmap, mesas, cliffs, rock formations and materials;
  - the seeded core;
  - 3 seeded spawns with opening waves;
  - line of sight and height-based range;
  - fog;
  - the camera.
- **With the test arena:** enemy pathfinding and mazing (issue #11).
- **Later:** art for the terrain (the style board first).
- **Out:** typed seeds in normal runs, and gameplay effects of materials (for now).

## Acceptance (what the tests check)

All in `src/core`, headless, in doctest:

- **Determinism:** the same seed gives an identical map. Golden hashes are pinned for 3 seeds.
- **Rules on synthetic maps:**
  - cliff steps block movement both ways;
  - formations block movement and building;
  - flat-footprint building, with cliff cells and formations refused.
- **Sweep over 1,000 seeds.** For each map:
  - the core is in the central third, on buildable ground;
  - 3 spawns, each reachable, at least 120 steps from the core and at least 60 cells apart;
  - every edge spawn touches the edge;
  - the terrain shares stay in bounds;
  - the fallback is never used.
- **Line of sight:**
  - over a ridge, a low tower is blocked and a high one sees;
  - formations block direct, lobbed and sight;
  - lobbed shots clear a ridge;
  - the corner rule.
- **Range:** the height bonus and penalty, including the cap and the floor.
- **Fog:**
  - exploring around the core at start;
  - exploring by sight over terrain;
  - no exploring by enemies;
  - explored cells stay explored;
  - spawns and enemies on unexplored cells are hidden.
- **Camera:** the default, zoom-out and zoom-in steps for 1920x1080, 1366x768 and 2560x1440, plus
  small and very wide windows.
- **Performance:** measure generation and the terrain bake.
  - **Proposed** target: under 50 ms per stage in a Release build on ANODE-MGMT.

## Open questions

1. The performance target for generation (Proposed: under 50 ms per stage).
2. **Pinned:** do slopes slow enemies?
3. For the arena or tower round: refusing a tower on unexplored ground (because a spawn or an
   enemy is there) would give its position away. Should building on unexplored ground be allowed,
   refused, or allowed with that leak accepted?

The remaining numbers are **(start)** values to tune in the arena.
