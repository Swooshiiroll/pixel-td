# Pixel TD: working in this repo

An endless roguelite tower defense game with retro-futuristic pixel art, written in C++20 with
SDL3 and CMake. Two people work on it: the owner, who decides and tests, and Claude, who
implements.

- Repo: https://github.com/Swooshiiroll/pixel-td
- Board: https://github.com/users/Swooshiiroll/projects/4
- Workflow page: https://claude.ai/artifact/T4JxzLByN4q8NrNfB8byrX

## Models

- **Planning** (plan mode: design rounds, bug triage, the plan itself): Opus 5.5, high effort.
- **Carrying out an approved plan:** Sonnet 5.5, medium effort.
- **Tweaks after a review** (fixes and adjustments the owner asks for once they've seen a result):
  Opus 5.5 again, through plan mode, not straight into code on Sonnet.

This is automatic: the owner's Claude Code user settings use `opusplan`, which switches from Opus to
Sonnet when a plan is approved, with the effort set per model. So plan in plan mode. If a session
is on the wrong model for the phase, say so; the owner switches with `/model`.

## Designing a feature first

For a **new feature or a rework** of an existing one (not bug fixes or small tweaks), design it
with the owner before writing any code.

1. **Ask questions, lots of them, in rounds.** Each round builds on the answers to the last. Keep
   going until nothing important is unclear.
   - Use multiple choice (AskUserQuestion) where there are clear options.
   - Ask open questions for creative matters: look, feel, names.
   - Never fill a gap with an assumption.

   Cover:
   - **Purpose:** what it's for, and the experience it should create.
   - **Rules and numbers:** limits, timings, defaults, how it scales.
   - **Interactions:** with the existing features, saved data and formats (a migration?), settings.
   - **UI and controls:** where it lives, feedback, what the user sees.
   - **Edge cases:** old data, empty and huge inputs, failure and offline cases.
   - **Scope:** what's out, and whether it ships in phases.
   - **Acceptance:** how we'll know it's right, and what the tests must check.
2. **Write the answers down.**
   - **Big features** (a new system, many files, a data-format change) go in `design/<feature>.md`,
     with an Open questions section.
   - **Small ones** get a spec in their issue.
   - Link the doc from the issue.
3. **Get the owner's confirmation** of the spec before creating the branch. Questions that come up
   while building go back to the owner too.
4. **Map the pull request to the spec.** Its description says which points it covers and any it
   leaves for later.

## Every change goes through a pull request

`main` is protected: it only takes pull requests whose CI checks pass. Never push to `main`.


1. **Start from an issue.** Find or create it (`gh issue list`, `gh issue create`).
   - Give it the templates' labels, and a milestone if it belongs to one.
   - Add it to the board with `--project "Pixel TD"`.
   - Columns: Backlog → Next → In progress → In review → Done. Move the issue as work moves
     (`gh project item-edit`, or drag it on the board).
2. **Branch:** `feat/<issue>-slug`, `fix/…`, `perf/…`, `design/…`, `docs/…` or `chore/…`.
3. **Work in small commits.**
   - **Unit tests:** every behaviour change adds or updates unit tests.
   - **Before pushing:** run `tools/dev.sh check` and `tools/dev.sh test`.
4. **Open the pull request:** `gh pr create`, using the template.
   - Put `Closes #<issue>` in it.
   - Add a line to `CHANGELOG.md` under Unreleased.
5. **Wait for CI:** `gh pr checks --watch`. Then give the owner the test-build link: the run's
   Artifacts, `pixel-td-<platform>-<branch>-<sha>`.
6. **Merge only when the owner says so:** `gh pr merge --squash --delete-branch`.

**Releasing:**
1. Move the Unreleased notes under a new `## [x.y.z] - date` heading.
2. Run `tools/dev.sh version x.y.z`.
3. Merge that pull request, then tag: `git tag vx.y.z && git push origin vx.y.z`.
4. The release workflow checks, tests, builds and publishes it, with that changelog section as the
   notes.

## Commands (`tools/dev.sh`, or `tools/dev.ps1` from PowerShell)

| Command | What it does |
|---|---|
| `setup` | First run on a fresh checkout (CI runs it too) |
| `check` | Static checks and version consistency (CI check `check`) |
| `test` | Unit tests (CI check `unit-tests`) |
| `build` | Builds into `build/` (CI check `build`; uploaded as the test build) |
| `version x.y.z` | Sets the version everywhere (`VERSION` is the source) |

Logs go to `out/`.

## Rules

- **Never push to `main`; merge only on the owner's go-ahead.**
- **Design before code** for features and reworks (above).
- **Backups:** before editing a critical file outside this repo, copy it to
  `D:\<Device>\<DriveLetter>\<path>`.

## Project-specific rules

- **Performance is the reason for the stack** (the owner chose C++/SDL3 for speed and optimisation
  headroom). Hot paths (enemies, projectiles, targeting) use flat arrays/pools with no per-frame
  heap allocation; measure before and after any optimisation.
- **`src/core` never includes SDL** or does I/O: it's the deterministic simulation and is what the
  unit tests link. Rendering, input and windows live in `src/game`.
- **Determinism:** gameplay randomness comes only from `pixeltd::Rng` seeded per run; the sim
  advances in fixed 60 Hz ticks (`FixedStep`). Never use wall-clock time or `rand()` in the sim.
- **Pixel-perfect view:** the world is drawn 1:1 in world pixels into a render target. The window
  shows it at a whole-number scale, and you never draw at a fractional scale. The one exception is
  zooming out past 1x (to 1/2 and the whole map): those steps draw shrunk, smooth copies.
  - **Grid:** a hidden grid of 16 px cells for terrain, line of sight and pathfinding; the terrain
    itself is drawn seamless (no tiles or grid lines). Towers and the core snap to an 8 px
    placement subgrid: a light tower is 3x3 subcells (24 px), a heavy tower 6x6 (48 px), and the
    core 9x9 (72 px).
  - **Map:** 320x180 cells (5120x2880 px), a heightmap.
  - **Zoom:** whole-number steps, then 1/2 and the whole map. The view fills the window at
    whole-number steps, and the default zoom shows about 640x360. See `design/world.md`.
  - **Pure maths stays in `src/core`.** That covers `core/camera.hpp`, and the front end only
    feeds it window sizes.
- **Map generation is deterministic across compilers.**
  - Integer maths only: no floats, no `sqrt`.
  - Randomness comes only from `pixeltd::Rng`, with sub-seeds derived by `splitmix64` and fixed
    stream constants.
  - Orderings are fixed (BFS queues; full tie-break keys for priority queues).
  - The golden-hash tests pin results on MSVC and GCC. If you change the generator on purpose,
    update the hashes in the same pull request and say why.
  - Every generator number lives in `GenParams` as a (start) value.
- **Art is generated, not drawn by hand or downloaded:** sprites are original, authored by Claude.
- **Dependencies are pinned** (SDL3 3.4.18 and doctest 2.5.3 by release URL + SHA256 in
  `CMakeLists.txt`). Upgrade deliberately, in their own PR.
- **clang-format is exactly 22.1.3** locally and on CI (`.clang-format`; `tools/dev.sh check`
  fails on any other version so formatting never flip-flops).
- **The owner plays over RDP:** the renderer must keep working on D3D11/WARP or software; the game
  logs the chosen backend at start.
- **Smoke run:** `out/stage/pixel-td/PixelTD.exe --frames 120 --screenshot <file>.bmp` renders and
  saves the 640x360 view, then exits.
- **Copies to the owner's devices.** The owner reviews and plays on other machines. Copies never
  overwrite: each delivery is a new file or folder. Check every copy's SHA256 afterwards.
  - **Design docs** (`design/*.md`, plus any HTML pages or style-board exports):
    - **Apollyon:** always, to `C:\Users\_Swooshii\Documents\Pixel TD`. Reach it with
      `ssh apollyon`; the remote shell is cmd.exe, so chain with `&`. Check with `certutil -hashfile`.
    - **Corundum-LX:** whenever it's up (`ssh -o ConnectTimeout=8 swooshii@corundum-lx`), to
      `~/Documents/Pixel TD Design`. If it's down, say the copy is owed.
    - **Names:** versioned, `Pixel TD - <Doc> (vN YYYY-MM-DD).md`.
    - **Review:** the owner may annotate a copy in place. Fetch the latest copy back and diff it
      against git to read the review.
  - **Linux test builds:** after every build, to LX
    `~/Documents/Pixel TD Testing/<YYYY-MM-DD> <HHMM> <branch with / as -> (<commit>)/`, with a
    `BUILD.txt` giving the branch, commit, version and date.
    - Linux can't be built here, so use the CI run's Linux artifact (`gh run download`).
    - Copy with tar over ssh, because scp mangles the spaces in the path.
  - **Windows builds:** to Apollyon `Documents\Pixel TD Testing\...`, the same way, when the owner
    says they're playing there.
