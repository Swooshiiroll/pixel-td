# World layout and map generator

Status: **confirmed by the owner** (PR #4, 2026-11-18), then **reworked by the owner the same day**
(issue #5). The world became a seamless heightmap. The seed now places the core and the spawns,
and enemies find their own way instead of following a generated path (see
[pathfinding](#enemies-and-routes)). The owner then resized the map to 5120 x 2880 px on 16 px
cells, with an 8 px placement subgrid and a zoom-out to the whole map (issue #15), and then to
15360 x 8640 px, 3 x 3 that size, drawn straight from the cells to keep memory small (issue #25).

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
| Map | **15360 x 8640 px**: nine 5120x2880 areas in a 3 x 3 layout (36 1440p screens) |
| Hidden grid | 16 x 16 px cells: **960 x 540 cells** |
| Placement subgrid | 8 x 8 px subcells: 1920 x 1080 (two per cell each way) |
| Screen at default zoom | about 640 x 360 px (40 x 22.5 cells), a 576th of the map; see Camera |
| Light tower | 3 x 3 subcells (24 px) |
| Heavy tower | 6 x 6 subcells (48 px) |
| Core | 9 x 9 subcells (72 px) |
| Spawn | 2 x 2 cells (32 px) **(start)** |

- **The look is seamless.** There are no tiles or grid lines; height is shaded smoothly across
  cells.
- **The grid is hidden** but real. Terrain, spawns, line of sight and pathfinding use its 16 px
  cells.
- **Towers and the core snap to the 8 px subgrid** and keep their sizes (24, 48 and 72 px). A
  footprint covers parts of several cells: a light tower always 2 x 2 cells, a heavy one 3 x 3 or
  4 x 4 depending on where it sits, and the core 5 x 5.
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
  terraces some regions into mesas: steps every **20 height units (start)**, each edge a sheer side
  (a cliff, since 20 is over the cliff step).
  - **Feathered:** terracing fades in over **8 cells (start)** from a mesa region's edge, so mesas
    blend into the rolling terrain instead of starting abruptly.
  - **Gentle tables:** each table keeps a compressed copy of the slope under it, up to **6 height
    units (start)**, the flat limit, so tables stay buildable. The step between tables is then
    20 − 6 = 14, still a cliff.
- **Noise layers (start values, tunable).** Height, the mesa mask, formation roughness and
  materials each come from a noise layer with the same controls: the octave count, the first
  octave's feature size, lacunarity (how much smaller each next octave is) and persistence (how much
  weaker), the interpolation curve (linear, smoothstep, quintic), domain warp (strength and size)
  and the shape (normal, ridged, billowed). The defaults: height 3 octaves from 64 cells (weights
  4 : 2 : 1), the others 1 octave; smoothstep, normal, no warp. Settings can be saved as presets
  (settings only, no seed).
- **Cliff faces.** A step between neighbouring cells greater than **12 height units (start)** is a
  cliff.
  - Enemies can't cross a cliff, in either direction.
  - A cell beside a cliff can't be built on.
- **Rock formations.** Large outcrops, **54–108 per map**, each about **10–32 cells across (start)**. They
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
- **Range follows the grade, not the altitude** (#27). What counts is how the ground falls or rises
  between a tower and its target, not how high the tower stands:
  - the **grade** is the fall per cell: (tower's ground - target's ground) x 16 / distance in px
    (a distance under one cell counts as one cell). It's positive downhill, negative uphill;
  - the range changes by **10% per unit of grade (start)**, at most **+-50% (start)**: longer downhill,
    shorter uphill, the same strength both ways;
  - so flat ground at any altitude gives the base range (a circle). A drop-off ahead stretches the
    reach and a climb ahead shrinks it, and on a slope the reach is an oval stretched downhill. For
    example, a light tower (72 px) over ground that falls 10 units in 4 cells reaches 90 px
    (+25%); 20 units in 4 cells, 108 px (+50%, the cap).
- Whether a weapon fires directly or is lobbed is a trait of the weapon part
  ([towers.md](towers.md)).

### How a map is made (run start)

All from the seed, at run start. There's no placement step: the run opens straight into the
first build phase.

1. **Terrain:** height, mesas, cliffs, rock formations and materials.
2. **The core:**
   - The seed picks a spot on the 8 px subgrid whose cells all lie in the **central third** of the
     map (cells x 106–213, y 60–119).
   - The spot must be buildable (see Building).
3. **Spawns:** the seed places **3**, each on a map edge or inside the map (chosen 50/50
   **(start)**).
   - **Reachable:** each can reach the core without crossing a cliff or a formation.
   - **Far enough:** each is at least **360 steps (start)** from the core by the shortest walk.
   - **Spread out:** spawns are at least **180 cells (start)** apart.
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
- **Gaps:** enemies fit through any 1-cell (16 px) gap. Pathfinding runs on the 16 px cells, and a
  tower blocks every cell its footprint touches, so the 8 px gaps the subgrid allows aren't
  walkable (open question 4).
- **Movement:** cliffs and rock formations block it.
- **Pinned:** whether slopes slow enemies.

### Building

- Towers (and the core) go on **fairly flat ground**: across every cell the footprint covers, the
  height range must be at most **6 units (start)**, with no cliff cell and no rock formation.
- Footprints snap to the 8 px subgrid.
- A tower stands at its ground's height, which sets its range and sight.

### Visibility (fog)

Fog works as **explored ground**:

- **The terrain is always visible:** height, cliffs and formations.
- **Unexplored ground has a light haze.** Spawns and enemies under it are hidden.
- **Exploring:**
  - **At run start,** every cell within one light tower's range of the core is explored. Until
    towers define their range, that's every cell whose centre is within 72 px of the core's edge
    **(start)**.
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
- **Zoom:** whole-number scale steps keep pixels crisp, then zooming out continues to the whole map.
  - **The default zoom** is the largest whole-number scale whose view is at least 640 x 360. For
    example, 3x on 1920x1080 gives exactly 640x360, and 2x on 1366x768 gives 683x384.
  - **Zooming in** goes one step closer than the default: 4x on 1080p, which shows 480x270.
  - **Zooming out**, the whole-number steps go down to 1x (or the last whole step whose view still
    fits inside the map, on windows bigger than it). Then:
    - **halving steps**, 1/2, 1/4, 1/8 and so on, while each still shows less than the whole map;
    - **the whole map**, fitted to the window. If the window's shape differs from the map's, the
      rest is dark bars.
  - **Pixel-perfect exception:** the steps below 1x (and any other fractional step) are drawn
    shrunk and **smooth**. These are the only views that aren't pixel-perfect. Play continues at
    every step.
  - At the whole-number steps the view **fills the window**, with no bars.
  - **Controls:** the mouse wheel zooms about the cursor, and + and - zoom about the centre of the
    screen.

| Window | Zoom steps (closest → farthest) |
|---|---|
| 1920x1080 | 4x, **3x (default)**, 2x, 1x, 1/2, 1/4, whole map (1/8) |
| 2560x1440 | 5x, **4x**, 3x, 2x, 1x, 1/2, 1/4, whole map (1/6) |
| 3840x2160 | 7x, **6x**, …, 1x, 1/2, whole map (1/4) |
| 1280x720 | 3x, **2x**, 1x, 1/2, 1/4, 1/8, whole map (1/12) |

**How the terrain is drawn.** Every cell gets one colour (brightness from height, hill shading,
muted material hue, formation shadows), stored in a 960 x 540 texture of about 2 MB. The GPU
stretches it with linear filtering, so each pixel blends the four nearest cell centres: the terrain
is seamless, and the whole map costs that one small texture at any size, with nothing to bake. At
whole-number steps it's drawn 1:1 in world pixels and then scaled up crisp, as before.
- The camera starts centred on the core.

### Developer tools

These exist for building and reviewing the generator. **Normal runs stay random-only**: these
are launch flags and keys for development, not settings in the game.

- `--seed N` generates the map from seed N (decimal, or hex with `0x`).
- **F5** makes a new map from a new random seed.
- The **HUD** shows the seed, the zoom, the generation time, and the cell under the cursor with its
  height.
- `--map-shot file.bmp` saves the whole map as an image (fully explored), then exits. It's saved at
  reduced scale: `--map-scale 2|4|8`, default 4 (3840 x 2160).
- **F** hides and shows the fog. While it's off, the haze is gone and hidden spawns show; exploring
  carries on underneath, and the HUD says "fog off". `--no-fog` starts with it off.
- `--zoom whole|half|N` starts at the whole-map step, the 1/2 step, or the whole-number scale N.
- **Probes** (P, or `--probe X,Y` in subcells) and **overlays** (O, `--overlay NAME`): see #12 and
  #13.
- **Map-tuning panel** (Tab; `--panel` opens it at start): the seed (hex box, previous, next,
  random), every generator number including each noise layer's octaves, curve, shape and warp, live
  stats (attempts, fallback, timings, cliff, formation and buildable shares, spawn walks) and
  presets. While a slider is dragged the map regenerates and redraws (at most every 120 ms). Presets are settings only, saved by name to `presets/<name>.txt` next to the
  game; `--preset FILE` generates with one at start (also for map shots). Making a preset the
  defaults is a pull request (#23).

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
  - 3 spawns, each reachable, at least 360 steps from the core and at least 180 cells apart;
  - every edge spawn touches the edge;
  - the terrain shares stay in bounds;
  - the fallback is never used.
- **Line of sight:**
  - over a ridge, a low tower is blocked and a high one sees;
  - formations block direct, lobbed and sight;
  - lobbed shots clear a ridge;
  - the corner rule.
- **Range:** the grade rule both ways, flat ground at any altitude, and the caps.
- **Fog:**
  - exploring around the core at start;
  - exploring by sight over terrain;
  - no exploring by enemies;
  - explored cells stay explored;
  - spawns and enemies on unexplored cells are hidden.
- **Placement subgrid:** the cells a footprint covers at even and odd positions, and a footprint
  refused if any covered cell is bad.
- **Camera:** the zoom steps for 1920x1080, 2560x1440, 3840x2160, 1280x720 and a window bigger than
  the map, plus small and very wide windows. Fractional zoom keeps the world pixel under the cursor
  in place, and the whole-map step centres the map.
- **Performance:** measure generation and the terrain bake.
  - **Proposed** target: under 50 ms per stage in a Release build on ANODE-MGMT.

## Open questions

1. The performance target for generation (Proposed: under 50 ms per stage).
2. **Pinned:** do slopes slow enemies?
3. For the arena or tower round: refusing a tower on unexplored ground (because a spawn or an
   enemy is there) would give its position away. Should building on unexplored ground be allowed,
   refused, or allowed with that leak accepted?
4. For the pathfinding round (#11): towers sit on the 8 px subgrid but enemies path on 16 px
   cells, so a tower blocks every cell it touches and 8 px gaps between towers aren't walkable.
   Keep that, or path on the subgrid?

The remaining numbers are **(start)** values to tune in the arena.
