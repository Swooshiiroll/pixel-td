# Pixel TD

An endless roguelite tower defense game with retro-futuristic pixel art, written in C++20 with SDL3.

## Development

How work is planned, built, tested and released: [workflow page](https://claude.ai/artifact/T4JxzLByN4q8NrNfB8byrX) and `CLAUDE.md`.
Board: https://github.com/users/Swooshiiroll/projects/4

```sh
tools/dev.sh setup   # once: configures CMake in out/cmake, downloads SDL3 + doctest (pinned)
tools/dev.sh check   # clang-format 22.1.3 + warnings-as-errors build
tools/dev.sh test    # unit tests (core library, headless)
tools/dev.sh build   # Release build, zipped into build/; unpacked copy in out/stage/pixel-td
```

**Prerequisites**
- **Windows:** Visual Studio 2022+ Build Tools (C++ workload), CMake 3.28+, Git for Windows (for
  Git Bash), clang-format 22.1.3 (bundled with the Build Tools' LLVM component, or
  `pipx install clang-format==22.1.3`).
- **Linux (Debian/Ubuntu):** `sudo tools/linux-deps.sh`, plus `pipx install clang-format==22.1.3`.

**Layout:** `src/core` (simulation, no SDL), `src/game` (SDL3 front end), `tests` (doctest),
`design` (design docs), `tools` (dev scripts).

**Running:** `out/stage/pixel-td/PixelTD`. Each launch generates a new random map.

- **Camera:** pan with WASD, the arrow keys, the window edges or a right-drag. Zoom with the
  mouse wheel, or with + and -: whole-number steps, then out to 1/2 and the whole map.
- **Keys:** Esc quits. Developer tools:
  - F5 makes a new random map;
  - P toggles probe placement (a left-click places a probe that shows a light tower's reach and
    sight, and lifts the fog);
  - O and Shift+O step through the overlays (the mountain, fertility and moisture noise layers);
  - F hides and shows the fog;
  - Tab opens the map-tuning panel (seed, every generator and noise setting, live stats, presets,
    and the enemies' and blocks' numbers).
- **Pathfinding (phase 1, placeholders):**
  - B cycles build mode: off, light block, heavy block. A left-click places a block, a right-click
    removes one.
  - E sends a walker from every spawn; Shift+E toggles a steady stream.
  - R shows each spawn's route to the core, with its walk time. A sealed spawn's route is orange,
    and the block its walkers will break is outlined.
  - Hovering a walker shows its speed, health and state. The HUD shows the core's health.
- **Test arena (phase 2, placeholders):**
  - T cycles test tower building: off, light tower, heavy tower. Towers are blocks that shoot:
    a left-click places one, a right-click removes it.
  - With no build mode on, a left-click on a tower selects it (its reach and readout stay shown,
    and the panel shows it). Hovering a tower shows its reach, DPS, damage, kills and targeting;
    G cycles its targeting (first, closest, strongest, last).
  - Space pauses; `.` steps one tick (Shift+`.` ten); `[` and `]` change the speed (1/4 to 8x).
  - The panel (Tab) has a wave section (walkers per spawn, interval, spawns, health and speed %,
    Send) with the last waves' summaries, the test towers' numbers, and named layouts (the map,
    blocks and towers) saved in `layouts/` next to the game.
  - `tuning.txt` next to the game holds every enemy, block, tower and route number. Edit it while
    the game runs and it reloads; the panel's "Save to tuning file" writes the current numbers.
  - The HUD shows the speed (or PAUSED), the latest wave, and the tick time, route build time,
    build waits, shots and towers.
- **Developer flags:**
  - `--seed N` uses seed N (decimal, or hex with `0x`).
  - `--map-shot f.bmp` saves the whole map and exits (fully explored, unless probes are given), at `--map-scale 2|4|8` (default 4: 3840x2160).
  - `--probe X,Y` places a probe with its top-left at subcell X,Y (the 8 px placement subgrid; repeatable).
  - `--zoom whole|half|N` starts at the whole map, the 1/2 step or the whole-number scale N.
  - `--look X,Y` starts with the camera centred on cell X,Y.
  - `--no-fog` starts with the fog hidden.
  - `--block X,Y` / `--heavy X,Y` place a light / heavy block at subcell X,Y (repeatable);
    `--send` sends walkers at start, `--stream` starts the stream, `--routes` shows the routes.
  - `--tower X,Y` / `--heavy-tower X,Y` place a light / heavy test tower (repeatable);
    `--layout FILE` starts in a saved layout; `--wave N` sends a wave of N per spawn at start;
    `--speed X` starts at sim speed X; `--pause-at T` pauses at sim tick T; `--select X,Y` selects
    the tower covering subcell X,Y.
  - `--window WxH` opens the window at that size (for screenshots).
  - `--preset FILE` generates with the settings in a preset file; `--panel` opens the tuning panel at start.
  - `--overlay NAME` starts in an overlay: `mountain`, `fertility` or `moisture`.
  - `--frames N --screenshot f.bmp` renders N frames, saves the last frame and exits.

See `design/world.md`, Developer tools.
