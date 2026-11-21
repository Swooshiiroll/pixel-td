# Design docs

One file per big feature: `design/<feature>.md`, written from the design question rounds and
confirmed by the owner before any code (see CLAUDE.md, "Designing a feature first").

Each doc has: purpose, rules and numbers, interactions, UI, edge cases, scope, acceptance, and an
**Open questions** section. Small features get their spec in the issue instead.

| Doc | What it covers |
|---|---|
| [game.md](game.md) | Overview: run structure, view, art, the tower design process, v0.1 build order |
| [world.md](world.md) | World layout and the map generator: grid, terrain, core zone, paths, line of sight, fog, camera |
| [towers.md](towers.md) | Tower system: chassis + weapon + module, evolution, gold Mk upgrades, draft, swaps |
| [pathfinding.md](pathfinding.md) | Enemy pathfinding and mazing: flow fields on the subgrid, slow ground, breaking through, placeholder walkers and blocks (#11) |
