# Pixel TD: the test arena and live tuning

Part of v0.1 step 2 ([game.md](game.md), v0.1 build order), issue #11 **phase 2**. Phase 1 (walkers,
blocks, routes and breaking through) is in [pathfinding.md](pathfinding.md). The arena is where
towers are designed by feel: tower parts (step 3) are picked by pitch and pick, then tuned here on a
real generated map, during a wave.

## Purpose

- **Tune by feel, not on paper:** every number is a (start) value in a text file the game reloads
  live, so a change shows up mid-wave.
- **Rerun the same test:** a setup (map, blocks and towers) can be saved and loaded by name, so a
  tuning change can be compared against the same maze and wave.
- **See what happened:** readouts show what each tower did and how a wave went.

## Decided (owner, 2026-11-20)

| Topic | Decision |
|---|---|
| What the arena holds | A **placeholder test tower** with tunable range, damage, fire rate, shot speed and health. Real parts replace it in step 3. |
| Sizes | **Light (24 px) and heavy (48 px)**, each with its own numbers. Towers are blocks too: walkers route round them and break them when sealed in. |
| Shots | **Projectiles** that travel and can miss; pooled, with no per-frame allocation |
| Targeting | **Switchable per tower:** first, closest, strongest, last |
| Sight | **The real rules:** a tower shoots only what it can see past rock, and lifts the fog like a probe (sight = range) |
| Live tuning | A **text file** next to the exe, reloaded when it changes on disk; the panel can save back to it |
| Time | **Pause and single-tick step, fast forward, slow motion.** No replay. |
| Waves | A **wave panel:** count, interval, which spawns, health and speed multipliers. E and Shift+E stay as quick sends. |
| Readouts | Per-tower damage, DPS and kills; a wave summary; range on hover; performance |
| Saving setups | **Named layouts:** the seed, the map settings, and the blocks and towers |
| How you get in | **The game is the arena.** It moves behind a flag or menu when the run loop arrives (step 4). |
| Rebuild hitch | Routes are **rebuilt in the background** and used from a fixed tick, so runs stay repeatable ([pathfinding.md](pathfinding.md), open question 1) |

**Start values, defaulted in planning (the owner can change any; all live in the tuning file):**

| Test tower | Light | Heavy |
|---|---|---|
| Range (and sight) | 72 px | 120 px |
| Damage per shot | 10 | 40 |
| Fire interval | 500 ms | 1500 ms |
| Shot speed | 480 px/s | 240 px/s |
| Health | 200 | 600 |

Light does 20 damage a second and heavy about 27. A walker (100 health) takes 10 light hits or 3
heavy hits.

## How it works

**Test towers** (`src/core/sim.*`). A tower is a block with a gun (`Block::armed`): same footprints,
placement rules, route costs and breaking.
- **Each tick,** a tower that's ready picks a target among the walkers within its range and in its
  sight (rock blocks it). Its targeting decides which:
  - **first:** nearest the core along its route (the least route cost left);
  - **last:** the most route cost left;
  - **closest:** nearest the tower;
  - **strongest:** the most health.
  - Ties go to the lowest walker id.
- **Shots** are aimed at the target's centre when fired, with no lead. A shot flies in a straight line
  at its speed, moving in steps of at most 8 px so it can't pass through a walker. It hits the first
  walker whose centre it passes within 8 px of, whichever walker that is. It's lost when it hits rock
  or leaves the map, and when it has flown 1.5 x the tower's range.
- **Readouts:** each tower counts its damage dealt (capped at the health the walker had left), its
  kills and its shots, and keeps the damage of the last 5 seconds for its DPS.

**Waves.** A wave sends `count` walkers from each chosen spawn, `interval` apart (0: all at once),
with health and speed scaled by a percentage. E sends a wave of 1 from every spawn. Walkers from the
stream (Shift+E) are loose: they're not counted in any wave.
- **Summary:** walkers sent, killed and leaked (reached the core), the core damage, and how long the
  wave took. A wave is done when its last walker is killed or gets through. The last 8 are kept.

**Background route builds.** A change (a block or tower placed, removed or broken, or the slowing
numbers changed) on tick T starts a rebuild on a worker thread at the end of T, from a snapshot of the
blocks. The new routes are used from the start of tick T + `route_delay_ticks` (20 ticks, 333 ms;
0 rebuilds on the sim's thread at the next tick, as phase 1 did).
- If the build isn't finished by then, the sim waits for it. That frame is late, but the result is
  always the same; the HUD counts these waits.
- A change during a build is picked up by the next build, which starts when the current one is used.
- **Until the new routes are used,** a walker whose next step would be into a block placed since the
  last build **waits** (state Waiting) instead of walking into it. A sealed-in walker on the breach
  route attacks whatever block is in its way.
- The worker only reads its own snapshot, so threading never changes the result: the tests check that
  threaded and same-thread builds give the same state hash.

**Live tuning file** (`src/core/sim_tuning.*`): `tuning.txt` next to the exe, every `SimParams` value
as `key = value` (for example `walker.speed_px_s = 32`, `tower.light.range_px = 72`,
`routes.delay_ticks = 20`), with `#` comments.
- It's written with the defaults if it's missing, and checked for changes every 500 ms.
- Unknown keys, bad values and out-of-range values (clamped) are reported in the HUD and the panel;
  the rest of the file still applies.
- The panel's "Save to tuning file" writes the current numbers over it.
- Placed blocks and towers keep the health they were placed with; every other number applies at once.

**Layouts** (`src/core/layout.*`): `layouts/<name>.txt` next to the exe, holding the seed, every
generator setting (as in a preset, each key starting `map.`), and the pieces:
`block = light 1155,600`, `tower = heavy 1200,640 first`. The tuning numbers aren't part of a layout.
Loading one makes its map and places its pieces; a piece that no longer fits is skipped and reported.

## UI and controls

- **T** cycles the tower build mode: off, light tower, heavy tower. **B** still cycles blocks; T, B
  and P (probes) are exclusive. A left-click places, a right-click removes.
- **Selecting:** a left-click on a tower (no build mode on) selects it; the panel shows its readouts
  and its targeting. **G** cycles the targeting of the tower under the mouse.
- **Hovering a tower** shows its range circle and the cells it can see, and its damage, DPS, kills
  and targeting.
- **Time:** Space pauses; `.` steps 1 tick and Shift+`.` 10 ticks; `[` and `]` change the speed
  through 1/4, 1/2, 1, 2, 4 and 8.
- **HUD:** the speed (or PAUSED), the latest wave, and performance: the average tick time, the last
  route build time, the build waits, and the walker, shot and tower counts.
- **Route lines** show "pending" while a rebuild hasn't been used yet.
- **Panel sections:** test towers (light and heavy numbers), wave (count, interval, spawns, health %,
  speed %, Send), wave summaries, the selected tower, layouts (name, Save, list, Load), and the tuning
  file (save, last reload's errors).
- **Developer flag:** `--layout FILE` starts in a saved layout.

## Scope

- **In:** everything above.
- **Out (later):** real tower parts (step 3), enemy types and routing per type, replays, a spatial grid
  for targeting (only if the bench says it's needed), lead on shots.

## Acceptance (what the tests check)

- **Towers:**
  - a tower kills a walker standing still in exactly health ÷ damage shots;
  - a tower behind rock doesn't fire;
  - each targeting picks the expected walker of three, and ties go to the lowest id;
  - a tower is a block: walkers route round it and break it when sealed in;
  - damage, kills and DPS are credited to the tower that fired.
- **Shots:** a slow shot misses a fast walker; a shot is spent at the end of its travel, and sooner
  when it hits rock; the pool never grows past the most shots in flight at once.
- **Waves:** count, interval, spawns, and health and speed scaling; the summary adds up (killed +
  leaked = sent) with the core damage; loose walkers aren't counted.
- **Background routes:** used exactly `route_delay_ticks` after the change; a walker waits at a new
  block until then; a change during a build is picked up by the next; threaded and same-thread builds
  give the same state, run after run.
- **Text formats:** the tuning file and layouts round-trip; bad lines are reported; random text never
  breaks the tuning file and the result is always valid.
- **Speed** (manual bench, `arena bench`, seed 1 on ANODE-MGMT): a tick with 162 walkers, 50 towers and
  their shots takes about 0.035 ms. With a block placed every half second at 60 Hz, none of 20
  background builds (about 130 ms each) made the sim wait, and the worst tick took 2.2 ms (it was
  100-135 ms with rebuilds on the sim's thread).

## Open questions

1. **Shot lead:** aim where the target will be? (Out for now; the miss is part of the test tower.)
2. **Targeting cost:** every tower checks every walker each tick it's ready. A spatial grid if real
   waves show it matters.
3. **Layout format and the map generator:** a layout keeps the generator settings, but a later change to
   the generator itself makes a different map from the same settings. Pin a generator version in the
   layout?
