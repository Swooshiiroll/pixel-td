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
