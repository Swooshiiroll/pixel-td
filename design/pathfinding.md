# Pixel TD: pathfinding and mazing

Part of v0.1 step 2 ([game.md](game.md), v0.1 build order), issue #11. Enemies find their own way
from the spawns to the core, and what you build shapes their route. This doc covers **phase 1**:
placeholder walkers, blocks to build mazes with, and the routes. **Phase 2**, the test arena's
tools (test towers, waves, pause and step, readouts, live tuning, layouts, and background route
builds), is in [test-arena.md](test-arena.md).

## Purpose

- Mazing is the core skill: you don't defend a fixed path, you make one.
- Sealing enemies in is allowed, but never safe: they break through where your wall is cheapest.
- Terrain matters to routes: fords, marshes and forests are slower, so open ground and crossings
  become the natural lanes.

## Decided (owner, 2026-11-18 and 2026-11-20)

| Topic | Decision |
|---|---|
| Phasing | Paths first (this doc), then the arena tools |
| Movement | Smooth sub-pixel movement over a shared **flow field**, rebuilt only when blocks change |
| Path grid | The **8 px subgrid**, walkers 16 px across: any 16 px gap is walkable wherever it sits; an 8 px gap isn't |
| Slow ground | Speed lost: **trees -15%, mud -20%, marsh -25%, shallow water -35%** (start). The slowest that applies to a cell counts. |
| Route cost | **Fastest route:** costs are travel times, so walkers go round slow ground when that's quicker |
| Sealed in | Enemies **break blocks**; placements are never refused for sealing a route. They take the **cheapest breach**: routes are planned as if blocks were passable at the time it takes to break them, and they attack the first block on that route. Only when no open route exists. |
| Enemies | One placeholder **walker**: speed, health, attack and core hit, all tunable |
| Building | Placeholder **light and heavy blocks** with the real footprints (24 and 48 px) and health, no weapons |
| The core | Takes damage when a walker reaches it; at 0 the game shows "core fallen" (the run doesn't end yet) |
| Routing per enemy type | Later, with the enemy design (runners, brutes, ...) |

**Start values, defaulted in planning (the owner can change any):**
- **Walkers:**
  - 2 cells/s (32 px/s), 100 health;
  - break blocks at 10 damage/s;
  - hit the core for 10.
- **Blocks:** light 200 health (one walker breaks it in 20 s), heavy 600.
- **Core:** 1000 health.
- **Spawning:** a key sends one walker from every spawn. Another toggles a stream: one per spawn every 3 s. All 3 spawns are open in phase 1.
- **Fog:** walkers are hidden on unexplored ground (they still walk there). Route lines show only for found spawns.
- **Placing a block is refused** on:
  - unbuildable ground;
  - the core, a spawn's cells, another block or a probe;
  - a walker (its 16 px square, or the step it's taking).
- **Collisions:** walkers don't collide with each other in phase 1.

## How it works

**Nodes.** A route node is a 16 px square at an 8 px position: node (x, y) is the square whose
top-left is subcell (x, y), 1919 x 1079 of them.
- A node can be stood on if every cell under it can be walked on and it doesn't overlap the core.
- In the open field, it also mustn't overlap a block.
- Its speed is that of the slowest cell under it.

**Steps.**
- **Neighbours:** a walker steps to one of 8 neighbours. A diagonal step is allowed only if both nodes beside the corner are free, so there's no squeezing past corners.
- **Costs:** a straight step costs 100 at full speed, a diagonal 141. Both are multiplied by 100 / the speed % of the node stepped onto, so costs are travel times.

**Targets.** The nodes touching the core's edge from outside. A walker standing on one has arrived:
it hits the core and is removed.

**Two fields,** built by Dijkstra from the targets outwards. Both are deterministic: fixed neighbour order and full tie-break keys.
- **Open:** blocks are walls. It uses a bucket queue (Dial's algorithm) sized to the largest step.
- **Breach:** blocks are passable. Stepping onto a node adds the break time of each block it overlaps that the previous node didn't. The break time is the block's health ÷ attack, in step-cost units, so a thicker wall costs more.
  - It's only built when something is sealed in: a spawn, or a walker, with no open route.

**Walkers.**
- **Each tick:** a walker on a node picks the neighbour with the lowest cost + step cost. It uses the open field if it can reach the core there, else the breach field. Then it walks towards that node's centre at its speed × the ground's %.
- **Movement carries over:** what's left of a tick's movement continues past each node, so walkers don't lose speed at every node.
- **Breaking:** on the breach route, if the next node is in a block, the walker stops and attacks that block. When the block breaks, the fields are rebuilt.
- **When the fields rebuild** (a block placed, removed or broken, or the slowing changed), every walker picks its next step again.
- **Rebuilds run in the background** (phase 2, [test-arena.md](test-arena.md)): a change on tick T is rebuilt on a worker thread and used from tick T + `route_delay_ticks` (20, start). Until then, a walker whose next step is into a new block waits (state Waiting).

**The sim** (`src/core/sim.*`, `flow.*`, `sim_params.*`):
- integer only and ticked at 60 Hz;
- the same commands always give the same state, pinned by a state hash in the tests;
- every number is a start value in `SimParams`, tunable in the panel.

## Developer views (phase 1)

- **Route lines:** the route from each found spawn to the core, with its length and walk time. A sealed spawn's line follows the breach route.
- **Breach marker:** when a spawn is sealed, the block its walkers will attack is outlined.
- **Walker stats on hover:** its speed (with slowing), health and state (walking, breaking).

## Acceptance (what the tests check)

- **Routes:**
  - on open ground they cost straight and diagonal steps;
  - a **16 px gap passes and an 8 px gap blocks**, at both subgrid alignments;
  - no corner cutting;
  - slow ground is gone round when that's quicker, and crossed when there's no way round;
  - targets touch the core.
- **Breach:**
  - a sealed route goes through the cheaper of two walls, and through a single wall rather than a double one;
  - the open route is used whenever there is one.
- **Sim:**
  - a walker reaches the core at its speed and hits it;
  - sealed in, it breaks a block in health ÷ attack seconds (±1 tick), then gets through;
  - placement refusals;
  - blocks reroute, and removing one reroutes back;
  - the core stops at 0 and reports fallen;
  - the stream's timing;
  - the same commands give the same state;
  - random valid numbers never break it.
- **Speed** (manual bench, `route bench`): a tick with 500 walkers takes about 0.02 ms. A route rebuild takes about **100-135 ms** on ANODE-MGMT (target 60 ms). Phase 2 moved rebuilds off the sim's thread (open question 1).

## Open questions

1. ~~**Rebuild time.**~~ **Resolved (owner, 2026-11-20): rebuild in the background** and use the result at a fixed tick, which keeps runs repeatable ([test-arena.md](test-arena.md)). A rebuild still takes about 100-135 ms; making it incremental stays an option if the 333 ms delay ever feels slow.
2. Routing per enemy type (runners, brutes, flyers), with the enemy design.
3. Walkers bumping into each other, and how crowds squeeze through gaps.
4. Building on unexplored ground ([world.md](world.md), open question 2): refusing it because a walker is there would give the walker away.
