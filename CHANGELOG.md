# Changelog

All notable changes to Pixel TD. Each pull request adds a line under Unreleased; a release moves
them under its version (see CLAUDE.md, "Releasing").

## [Unreleased]

### Added
- Project set up: CI (check, unit tests, Linux + Windows test builds), tag releases, issue and pull request templates.
- Engine skeleton: SDL3 window with a pixel-perfect 640x360 view (32 px tiles, whole-number scaling), fixed 60 Hz simulation clock, PCG32 seeded RNG, doctest unit tests.
- README and CLAUDE.md link the project board and the workflow page (#1).
- Design docs for review: game overview, world layout and map generator, tower system (#3).
- Map generator, stage 1: seeded terrain (ground, high ground, rocks, cliffs) and a validated core zone on a 12 px grid; camera with whole-number pan and zoom; developer seed tools (`--seed`, F5, `--map-shot`) (#5).
- Map generator, stage 2: place the core in the zone (green or red ghost) and the spawn and path are generated: seeded lattice routing around terrain, 240 to 400 cells long, varying width with chokepoints and wide stretches, a ramp for a high-ground core, and the build-space guarantee; `--core auto` (#6).
- Line of sight (rocks block direct fire from low ground; high ground and lobbed weapons fire over rocks; cliffs block everything) and explored-ground fog (a haze over unexplored low ground; the core explores its surroundings and ramp; sight is always direct); developer probe (P, `--probe`) with a range and line-of-sight preview (#7).
