# Changelog

All notable changes to Pixel TD. Each pull request adds a line under Unreleased; a release moves
them under its version (see CLAUDE.md, "Releasing").

## [Unreleased]

### Added
- Project set up: CI (check, unit tests, Linux + Windows test builds), tag releases, issue and pull request templates.
- Engine skeleton: SDL3 window with a pixel-perfect 640x360 view (32 px tiles, whole-number scaling), fixed 60 Hz simulation clock, PCG32 seeded RNG, doctest unit tests.
- README and CLAUDE.md link the project board and the workflow page (#1).
- Design docs for review: game overview, world layout and map generator, tower system (#3).
- Map generator: a seeded 3840x2160 heightmap drawn seamless (brightness = height, hue = material) with mesas, cliff faces and towering rock formations; the core placed by the seed in the central third and 3 seeded spawns (edge or inside, opening at waves 1, 10, 20); camera with whole-number pan and zoom; developer seed tools (`--seed`, F5, `--map-shot`) (#5).
- Line of sight and range over the height field (terrain blocks direct fire and sight, rock formations block every shot, lobbed shots arc over terrain; range grows with height above the target); explored-ground fog that hides spawns until found; developer probes (P, `--probe X,Y`) with a reach-and-sight preview (#7).
- Developer overlays (O / Shift+O, `--overlay NAME`): elevation as colour bands with contour lines, and each of the generator's noise layers (three height octaves, combined height, mesa mask, formation roughness, material) in greyscale (#13).
- The map is 5120x2880 (2x2 1440p screens) on 16 px cells, with towers and the core snapping to an 8 px placement subgrid (sizes unchanged); zooming out continues past 1x to 1/2 and the whole map, drawn smooth; F hides and shows the fog (developer tool), and the fog is now its own layer, so lifting it no longer re-bakes the terrain (#15).
- Tuning: the downhill range bonus has no cap (it grows with the drop, +5% per 10 height units); mesas terrace every 20 height units instead of 40; the elevation overlay has a colour stop every 20 units (#19).
- Smoother mesas (terracing fades in over 8 cells from a mesa's edge, and table tops keep up to 6 units of the slope under them, so they stay buildable); every noise layer gets tunable octaves (count, size, lacunarity, persistence), curve, domain warp and shape; generator presets as text (settings only) (#21).
- Map-tuning panel (Tab, developer tool, Dear ImGui 1.93.0): seed, every generator and noise setting with a live low-detail preview while dragging, live stats, and named presets (settings only); `--preset FILE` and `--panel` (#23).
- The map is 3 x 3 bigger: 15360x8640 (960 x 540 cells), drawn straight from a cell-sized texture stretched by the GPU, so the game uses less than half the memory it did at a ninth of the size; zooming out halves down to the whole map; the elevation overlay draws contour lines as lines; spawns sit farther from the core (360 / 180, start values) and formations keep their density (54-108); map shots save at reduced scale (`--map-scale`) (#25).
- Tower range follows the grade between tower and target (the fall or rise per cell), not the tower's altitude: flat ground at any height gives the base range, a decline stretches the reach and an incline shrinks it, up to +-50% both ways (start values); the tuning panel has a towers, sight and range section (#27).

### Changed
- Faster CI: the check and unit-test jobs are merged into one core-only job (no SDL), and tools, apt packages and compiled objects are cached between runs; the unit tests only run when a push changes a system or object (#17).
