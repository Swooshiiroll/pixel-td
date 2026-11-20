# World layout and map generator

Status: **confirmed by the owner** (PR #4, 2026-11-18), then **reworked by the owner the same day**
(issue #5). The world became a seamless heightmap. The seed now places the core and the spawns,
and enemies find their own way instead of following a generated path (see
[pathfinding](#enemies-and-routes)). The owner then resized the map to 5120 x 2880 px on 16 px
cells, with an 8 px placement subgrid and a zoom-out to the whole map (issue #15), and then to
15360 x 8640 px, 3 x 3 that size, drawn straight from the cells to keep memory small (issue #25).

**Then the owner dropped height (issue #30, 2026-11-19):** "Lets drop the altitude idea and leave in
the rock formations". The map is now **flat ground with rock formations**, the only terrain obstacle,
and twice as many of them. The height-based design and code (hills, mesas, cliffs, sight and range
over height, and #29's simplex and erosion experiments) are kept in
[`archive/heightmap/`](../archive/heightmap/README.md).

**Then a colony-sim layout (issue #32, 2026-11-19):** the owner: "Make the map look like rimworld".
The flat map gets mountains with caves, a river and lakes, varied ground and forests (see Terrain);
mountains replace the rock formations. Our own generator and placeholder look, no copied art.

Numbers marked **(start)** are starting values. They live in the live-tuning data file and get
adjusted by feel in the test arena; the owner chose this over settling them on paper.

## Purpose

Every run takes place on a fresh map generated from a random seed: flat ground with mountains, a
river and lakes, forests and open fields, around a core near the centre. You read the land to
decide where to build. Rock blocks sight and direct fire; rock and deep water shape where enemies
can walk (fords are the river crossings), and your towers shape the rest of their route.

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

- **The look is seamless.** There are no tiles or grid lines; the ground's colour blends smoothly
  across cells.
- **The grid is hidden** but real. Terrain, spawns, line of sight and pathfinding use its 16 px
  cells.
- **Towers and the core snap to the 8 px subgrid** and keep their sizes (24, 48 and 72 px). A
  footprint covers parts of several cells: a light tower always 2 x 2 cells, a heavy one 3 x 3 or
  4 x 4 depending on where it sits, and the core 5 x 5.
- There's one map per run, and it's fixed for the whole run.
- The view is flat top-down, and the setting is a planet-surface outpost.

### Terrain

The ground is **flat**, laid out like a colony-sim map (#32): mountains with caves, a river and
lakes, several kinds of ground, and forests. It's our own generator and placeholder look; the art
comes from the style board. Every cell has:

| Layer | What it is | Gameplay |
|---|---|---|
| **Ground** | Soil, rich soil, sand, gravel, mud, marsh, shallow water, deep water | See the rules below |
| **Plant** | None, grass, bush or tree | Trees block building; grass and bushes are looks |
| **Rock** | Part of a mountain or outcrop, or open ground | Blocks walking, building, sight and direct fire |

**Rules:**

| | Walk | Build | Sight and direct fire |
|---|---|---|---|
| Soil, rich soil, sand, gravel | yes | yes | pass |
| Mud, marsh | yes (will slow enemies) | no | pass |
| Shallow water | yes (will slow enemies) | no | pass |
| Deep water | no | no | pass |
| Tree (on soil) | yes | no | pass |
| Rock | no | no | blocked (**lobbed shots arc over**) |

"Will slow enemies" waits for the enemy design (open question 4).

**How it's made (start values, all tunable in the panel and saved in presets):**
- **Mountains:** about **20%** of the map, **mostly near the edges** (the noise gets a bias that
  fades over the outer 45% of half the map's height), rounded off by cellular automata. Never within
  **16 cells** of the core area. Small outcrops (**1%**) dot the open ground; pieces of rock under
  **12 cells** are cleared.
- **Caves:** deep inside the mountains (5+ cells from open ground) a noise opens pockets; each one
  of **20+ cells** gets a 2-cell-wide tunnel to the outside, the rest are filled back.
- **River:** on **80%** of maps, from one edge to the opposite one along the cheapest path (by a
  noise, and dearer through rock, so it winds and skirts mountains). **6 cells** of deep water with
  **3-cell** shallow banks, and **2-4 fords**, each about **6 cells** of shallow water across.
- **Lakes:** **3-8** noise-edged blobs, radius **8-22 cells**, with deep middles and **2-cell**
  shallow rims.
- **Ground:** within **4 cells** of water, moist ground (**45%+** moisture) is mud, or marsh from
  **65%**; within **2 cells** of rock it's gravel; elsewhere fertility picks sand (under **20%**),
  rich soil (from **72%**) or soil.
- **Plants:** trees grow in forests, by a forest noise: none below **55%**, full density **10%**
  above that (**95%** of rich-soil cells, **75%** of soil); none on sand, gravel, wet ground or
  water. Grass on fertile soil and a few bushes, for looks.
- **No sealed pockets:** every walkable cell can be reached from the core. Anything cut off becomes
  rock (or deep water, beside water).
- **Noise:** every layer is simplex noise (no square look), with octaves, lacunarity, persistence,
  warp and shape; each octave is sampled on a grid matched to its feature size and blended, which
  keeps generation to about **210 ms** on ANODE-MGMT.

The rock formations of #30 are gone: mountains and outcrops replace them.

### Line of sight and range

- **Sight and direct fire** need a clear line: any rock cell on it blocks. Water and trees don't.
  - The line is checked centre to centre. Where it passes exactly through a cell corner, both
    cells beside it count.
  - The shooter's and the target's own cells never block (so a rock's face is seen).
- **Lobbed weapons arc over rock:** nothing blocks them.
- **Sight is always direct**, even for lobbed weapons. A lobbed tower can hit what it can't see,
  but only once something else has explored it: artillery needs a spotter.
- **Range is a plain circle**, the same everywhere: a light tower reaches **72 px (start)**. (Range
  by the grade of the ground, #27/#28, went with the heightmap.)
- Whether a weapon fires directly or is lobbed is a trait of the weapon part
  ([towers.md](towers.md)).

### How a map is made (run start)

All from the seed, at run start. There's no placement step: the run opens straight into the
first build phase.

1. **Terrain:** fertility and moisture, mountains, caves, the river, lakes, ground, plants, then
   sealing off pockets (see Terrain).
2. **The core:**
   - The seed picks a spot on the 8 px subgrid whose cells all lie in the **central third** of the
     map (cells x 106–213, y 60–119).
   - The spot must be buildable (see Building).
3. **Spawns:** the seed places **3**, each on a map edge or inside the map (chosen 50/50
   **(start)**).
   - **On dry land and reachable:** not in water, and each can walk to the core (rivers have fords).
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
- **Movement:** rock and deep water block it; shallow water, mud and marsh will slow it.

### Building

- Towers (and the core) go on buildable ground: every cell the footprint covers must be soil, rich
  soil, sand or gravel, with no tree and no rock.
- Footprints snap to the 8 px subgrid.

### Visibility (fog)

Fog works as **explored ground**:

- **The terrain is always visible:** the ground, water, plants and rock.
- **Unexplored ground has a light haze.** Spawns and enemies under it are hidden.
- **Exploring:**
  - **At run start,** every cell within one light tower's range of the core is explored. Until
    towers define their range, that's every cell whose centre is within 72 px of the core's edge
    **(start)**.
  - **After that,** a tower explores every cell within its range (a circle) that it can see past
    the rock.
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

**How the terrain is drawn.** Every cell gets one colour (the ground's colour, darker under trees, or rock
shaded by its looks value, plus the shadows the rock casts), stored in a 960 x 540 texture of about 2 MB. The GPU
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
  material and whether it's rock or buildable.
- `--map-shot file.bmp` saves the whole map as an image (fully explored), then exits. It's saved at
  reduced scale: `--map-scale 2|4|8`, default 4 (3840 x 2160).
- **F** hides and shows the fog. While it's off, the haze is gone and hidden spawns show; exploring
  carries on underneath, and the HUD says "fog off". `--no-fog` starts with it off.
- `--zoom whole|half|N` starts at the whole-map step, the 1/2 step, or the whole-number scale N.
- **Probes** (P, or `--probe X,Y` in subcells) and **overlays** (O, `--overlay NAME`: `rough` or
  `material`): see #12 and #13. The height overlays (elevation, octaves, mesas) went with the
  heightmap.
- **Map-tuning panel** (Tab; `--panel` opens it at start): the seed (hex box, previous, next,
  random), every generator number including each noise layer's octaves, curve, shape and warp, live
  stats (attempts, fallback, timings, rock, water, tree and buildable shares, spawn walks) and
  presets. While a slider is dragged the map regenerates and redraws (at most every 120 ms). Presets are settings only, saved by name to `presets/<name>.txt` next to the
  game; `--preset FILE` generates with one at start (also for map shots). Making a preset the
  defaults is a pull request (#23).

The test arena will get proper seed entry later.

## Interactions

- **Towers ([towers.md](towers.md)):**
  - Footprints use the hidden grid and need buildable ground (no water, wet ground, trees or rock).
  - Range is a circle; rock blocks sight and direct fire.
  - Whether a weapon is lobbed is a trait of the part, but sight is always direct.
  - Towers explore fog within their reach and sight.
- **Enemies and pathfinding:** they walk around rock, deep water and towers, crossing rivers at fords (issue
  #11).
- **Test arena:** uses this generator, with a seed you type in.
- **Saved data:** none in v0.1. A map is fully rebuilt from its seed.

## UI

- **Run start:**
  - The camera is centred on the core.
  - The core's surroundings are explored; everything else is under the haze.
- **Range preview** (while placing a tower): the cells in its range that it can see are tinted, and
  cells hidden behind rock are shaded.

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
  - flat ground: mountains with caves, a river and lakes, ground types and plants;
  - the seeded core;
  - 3 seeded spawns with opening waves;
  - line of sight past rock, and circular range;
  - fog;
  - the camera.
- **With the test arena:** enemy pathfinding and mazing (issue #11).
- **Later:** art for the terrain (the style board first).
- **Out:** typed seeds in normal runs, and gameplay effects of materials (for now).

## Acceptance (what the tests check)

All in `src/core`, headless, in doctest:

- **Determinism:** the same seed gives an identical map. Golden hashes are pinned for 3 seeds.
- **Rules on synthetic maps:**
  - rock and deep water block walking; shallow water, mud, marsh and trees block building, not
    walking;
  - footprints covering any unbuildable cell are refused.
- **Sweep over 1,000 seeds.** For each map:
  - the core is in the central third, on buildable ground;
  - 3 spawns, each reachable, at least 360 steps from the core and at least 180 cells apart;
  - every edge spawn touches the edge;
  - every walkable cell is reachable from the core (no sealed pockets);
  - mountains near their share, and denser by the edges; rivers edge to edge with their fords;
    caves on most maps;
  - the fallback is never used.
- **Line of sight:**
  - rock blocks sight and direct fire, and lobbed shots arc over it; water and trees don't block;
  - rock beside the line doesn't block;
  - the corner rule, and the end cells never block.
- **Range:** a circle, in at its radius and out just past it.
- **Fog:**
  - exploring around the core at start;
  - exploring a circle by sight, with rock casting sight shadows;
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

1. The performance target for generation. With the colony-sim terrain it's about 210 ms on
   ANODE-MGMT (the heightmap took 140 ms, the flat map 55 ms).
2. For the arena or tower round: refusing a tower on unexplored ground (because a spawn or an
   enemy is there) would give its position away. Should building on unexplored ground be allowed,
   refused, or allowed with that leak accepted?
3. For the pathfinding round (#11): towers sit on the 8 px subgrid but enemies path on 16 px
   cells, so a tower blocks every cell it touches and 8 px gaps between towers aren't walkable.
   Keep that, or path on the subgrid?
4. With the enemy design: how much shallow water, mud and marsh slow enemies (#32 says they will).

The remaining numbers are **(start)** values to tune in the arena.
