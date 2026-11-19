# Archive: the heightmap terrain

This folder isn't built, formatted or tested: CMake, `tools/dev.sh check` and `tools/ci-scope.sh` only
look at `src/`, `tests/` and the CMake files. It's kept so the height-based terrain can be studied
or brought back.

## Why it was removed

On 2026-11-19 the owner compared three versions of the height-based terrain (#29): value noise with
mesas, simplex noise, and simplex noise with hydraulic erosion. They decided: "Lets drop the
altitude idea and leave in the rock formations". In #30 the map became flat ground, with more rock
formations as the only terrain obstacle. Sight and direct fire are blocked by formations, and a
tower's range is a plain circle.

## What's here

These are snapshots of whole files at their last version, at the same paths they had in the repo.

**From main at `1653e63` (the last commit before #30), in `src/`, `tests/` and `design/`:**

| File | What it held |
|---|---|
| `src/core/mapgen.cpp` | `make_height`: value-noise octaves stretched to 0..200, and mesas (a noise mask, terraces every 20 units, an 8-cell feather, table relief). Formations stood at 210-255. The cliff share check. |
| `src/core/terrain.{hpp,cpp}` | Cliffs: a step bigger than 12 between neighbours blocks walking and building. The flat-ground rule for footprints (a height range of 6 or less). |
| `src/core/los.{hpp,cpp}` | Line of sight over the height field (eye +10, target +4). Range by the grade along each line (#27/#28): 10% per unit of fall per cell, ±50%. `isqrt`. |
| `src/core/fog.{hpp,cpp}` | Exploration by grade-adjusted reach and sight over height. |
| `src/core/gen_params.{hpp,cpp}` | Every height, mesa, cliff, sight and range number, with `validate()`. |
| `src/core/gen_util.hpp` | Value noise and `fill_noise`, recording the height octaves for the overlays. |
| `src/core/overlay.{hpp,cpp}` | The elevation colour bands and the octave, height and mesa overlays. |
| `src/core/contours.{hpp,cpp}` | Contour lines by marching squares, chunked (#25). |
| `src/game/map_view.cpp` | Drawing height as brightness, hill shading lit from the north-west, the elevation overlay and contours, and the probe preview by grade. |
| `src/game/tuning_panel.cpp` | The Height, Mesas, Cliffs and range sections of the panel. |
| `tests/*.cpp` | Their tests, including the golden hashes of the height maps. |
| `design/world.md` | The world design as it was: heightmap, mesas, cliffs, and line of sight and range over height. |

**From the unmerged #29 branch at `ab384e3`, in `terrain-29/`:**

| File | What it held |
|---|---|
| `src/core/simplex.{hpp,cpp}` | 2D simplex noise in fixed point: a triangular lattice, 24 gradients, corners hashed with the seed. |
| `src/core/erosion.{hpp,cpp}` | Particle hydraulic erosion in fixed point. Sediment is conserved exactly, with what's lost counted. |
| `src/core/gen_params.*`, `gen_util.hpp`, `mapgen.*`, `preset.*` | The terrain picker (value, simplex or eroded), the erosion numbers, simplex as a noise basis, and `natural_cliff_share_min_pct`. |
| `src/game/tuning_panel.cpp` | The panel's terrain picker and Erosion section. |
| `tests/test_{simplex,erosion,mapgen}.cpp` | Their tests: balance, continuity, conservation and gullies. |

The #29 comparison measured, over 4 seeds:

| | Generation | Cliff cells | Buildable |
|---|---|---|---|
| Value | 155 ms | 2.2% | 93% |
| Simplex | 375 ms | 0.07% | 89% |
| Simplex + erosion | 1.6 s | 1.2% | 75% |

## Bringing it back

The simplest way is from git: check out `1653e63` (or the `feat/29-natural-terrain` branch, kept on
GitHub) and merge forward. These copies are for reading without digging through history.

To restore by hand:
- `Map` needs its `height` grid back.
- The fog, the probe preview and the panel need the grade-based reach again.
- The golden hashes need re-pinning.
