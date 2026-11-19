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
  - O and Shift+O step through the overlays (elevation, and the generator's noise layers);
  - F hides and shows the fog.
- **Developer flags:**
  - `--seed N` uses seed N (decimal, or hex with `0x`).
  - `--map-shot f.bmp` saves the whole 5120x2880 map and exits (fully explored, unless probes are given).
  - `--probe X,Y` places a probe with its top-left at subcell X,Y (the 8 px placement subgrid; repeatable).
  - `--zoom whole|half|N` starts at the whole map, the 1/2 step or the whole-number scale N.
  - `--no-fog` starts with the fog hidden.
  - `--window WxH` opens the window at that size (for screenshots).
  - `--overlay NAME` starts in an overlay: `elevation`, `octave1`, `octave2`, `octave3`, `height`, `mesa`, `rough` or `material`.
  - `--frames N --screenshot f.bmp` renders N frames, saves the last frame and exits.

See `design/world.md`, Developer tools.
