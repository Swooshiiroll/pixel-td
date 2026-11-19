# Changelog

All notable changes to Pixel TD. Each pull request adds a line under Unreleased; a release moves
them under its version (see CLAUDE.md, "Releasing").

## [Unreleased]

### Added
- Project set up: CI (check, unit tests, Linux + Windows test builds), tag releases, issue and pull request templates.
- Engine skeleton: SDL3 window with a pixel-perfect 640x360 view (32 px tiles, whole-number scaling), fixed 60 Hz simulation clock, PCG32 seeded RNG, doctest unit tests.
- README and CLAUDE.md link the project board and the workflow page (#1).
- Design docs for review: game overview, world layout and map generator, tower system (#3).

### Changed
- Faster CI: the check and unit-test jobs are merged into one core-only job (no SDL), and tools, apt packages and compiled objects are cached between runs (#17).
