# Tower system

Status: **draft for the owner's review** (issue #3). Decided in the question rounds of 2026-11-18,
and the open questions were reviewed with the owner the same day.

Numbers marked **(start)** are starting values. They live in the live-tuning data file and get
adjusted by feel in the test arena; nothing on a part is hard-coded.

## Purpose

Bastion Line's towers were **too many to hold in mind** and **slow to design**. Pixel TD fixes both
with modular parts:

- **Parts decide behaviour.** A tower is a few small parts, each with one job, so the whole
  roster fits in your head while every run still finds new combinations.
- **Evolution only boosts stats.** Towers grow by being used, so growth doesn't need a hand-made
  upgrade tree for every tower.
- **Gold Mk upgrades** add a short, deliberate track on top. Only the last step is a big change.

The owner's rule: "Modular parts can change the way a tower behaves, while evolving boosts stats."

## Rules

### Parts

Every tower is exactly **one chassis + one weapon + one module**, in three fixed slots.

| Part | What it decides |
|---|---|
| **Chassis** | Footprint and mount size (light or heavy), range, turn speed, and the tower's **evolution level cap** |
| **Weapon** | How it hurts (examples: beam, shell, arc, pulse). It's light or heavy, and either **direct fire** or **lobbed** (line of sight, [world.md](world.md)) |
| **Module** | The twist (examples: chains, slows, splits, pierces, overcharges). It fits any chassis |

**Mount sizes:**

| Chassis | Footprint | Takes |
|---|---|---|
| Light | 3 x 3 subcells (24 px) | Light weapons |
| Heavy | 6 x 6 subcells (48 px) | Light **or** heavy weapons |

The examples are placeholders. The real parts come from pitch and pick (see Designing parts).

### Getting towers in a run

- **Starting kit:** every run begins with **1 light chassis, 1 light weapon and 1 module** in the
  pool, so you can build before wave 1.
- **The draft unlocks parts.** Between waves you draft a chassis, a weapon or a module into your
  run's **part pool**. Unlocked parts are reusable.
- **Gold builds towers.** You build any legal combination of the parts in your pool, wherever
  there's legal ground ([world.md](world.md)).
- **Cost:** a tower costs **the sum of its three parts' costs** (each part's cost is **(start)**).

### After a tower is built

- **The chassis and weapon are permanent.**
- **The module can be swapped** for another one in your pool, for a gold cost **(start)**.
  - The tower **keeps its XP and level**.
  - The new module starts at Mk I.
  - 50% **(start)** of the gold spent on the old module's Mk upgrades is refunded.
- **Selling:**
  - A tower can be sold for part of what was spent on it (its parts plus its Mk upgrades). The
    share is **(start)**.
  - Its XP is lost.

### Evolution (stats only)

- **XP = damage dealt.**
- **Five stats can grow:**
  - **damage**;
  - **fire rate**, which includes turn speed;
  - **range**;
  - **penetration**;
  - **effect strength**: the module's twist, such as chain count, slow % or split count.
- **The lean:** each point of damage feeds one stat's counter, depending on the situation.
  - Damage to an armoured target feeds penetration.
  - A hit in the outer third of the tower's range feeds range.
  - A hit on a fast enemy feeds fire rate.
  - Part of a hit on 2+ enemies (chain, split or area) feeds effect strength.
  - Anything else feeds damage.
  - If several situations apply, the damage is split equally between them.
  - What counts as "armoured", "fast" and "outer third" is **(start)**.
- **Level-up:**
  - **The biggest counter wins.** That stat gets the whole boost, and all counters reset.
  - The size of each boost is **(start)**.
- **Lean meter:** a meter on the tower shows the counters, so you can see where it's leaning.
- **Behaviour:** evolution never changes it. That's the parts' job.
- **Level cap:**
  - **The chassis sets it.** For example, a light chassis caps early and a heavy one goes higher.
  - The XP curve and the caps are **(start)**.
  - **At the cap,** the tower stops gaining XP. Anything past the cap is lost.

### Gold Mk upgrades (per tower)

- Gold upgrades **one placed tower's weapon or module**. The chassis isn't upgraded.
- Each weapon and each module has **three steps**, with costs **(start)**:

| Step | What it is |
|---|---|
| Mk II | A small, simple change (e.g. a stat bump) |
| Mk III | A small, simple change |
| **Mk IV** | The **one major change**: a new look, stat changes **and** a new behaviour |

- Mk II and III don't need to change the sprite, and names don't need to share a vocabulary.
  Bastion Line's visible-rebuild rule doesn't apply here.

### Sprites

Towers are **assembled from part sprites**:
- the chassis is the base;
- the weapon is a turret on top that rotates to aim;
- the module is a small add-on.

The game stacks them, so art grows with the number of parts, not combinations. Mk IV redraws only
the part it upgrades.

### Designing parts

1. **Pitch:** the owner describes a role or a feeling ("punishes fast enemies", "a 1950s
   raygun").
2. **Pitch page:** Claude makes a short page with **3 variants**: a sprite, a one-line role and a
   few numbers each. The owner picks one.
3. **Arena:** the pick goes into the test arena as data and is tuned by feel with the live sliders.
   This needs the arena, which comes first in v0.1 (see [game.md](game.md)).

## Interactions

- **World ([world.md](world.md)):**
  - footprints on the 8 px placement subgrid, covering no rock formation cell (the map is flat,
    #30);
  - range is a plain circle, the same everywhere;
  - direct fire needs a clear line past rock formations, while lobbed weapons arc over them;
  - towers explore fog within their range, by direct sight. That includes lobbed towers, which
    need a spotter to hit enemies behind a rock formation.
  - Towers also shape the enemies' route (mazing), and enemies break them if sealed in
    (issue #11).
- **Economy:** gold pays for building, module swaps and Mk upgrades. Selling and swaps refund part
  of it. Income is open (see [game.md](game.md)).
- **Draft:** the draft offers parts, starting from the kit. How many choices it offers, and how
  often, belong to the run-loop design.
- **Live tuning:** every number on a part lives in the hot-reloaded data file:
  - stats and costs;
  - lean thresholds;
  - boost sizes;
  - XP curve and level caps;
  - refund shares.

## UI

- **Build menu:** pick a chassis, a weapon and a module from the pool.
  - A preview shows the footprint, the reach (a circle; cells hidden behind rock formations are
    shaded) and the total cost.
  - Weapons that don't fit the chassis are greyed out.
- **Tower panel:**
  - the three parts with their Mk levels;
  - level and XP, the lean meter and the cap;
  - Mk upgrade buttons (weapon, module);
  - Swap module and Sell, each showing its refund.

## Edge cases

- **A heavy weapon on a light chassis** can't be picked. The menu greys it out.
- **Swapping to the same module** isn't offered.
- **A tower at its level cap** stops gaining XP. The lean meter freezes.
- **The pool never leaves you unable to build:** the starting kit is always one legal light tower.

## Scope

- **v0.1:**
  - the part model and the starting kit;
  - building from the pool;
  - module swaps and selling;
  - evolution;
  - Mk II–IV;
  - **8 parts: 2 chassis (1 light, 1 heavy), 3 weapons and 3 modules**, made by pitch and pick
    and tuned in the arena.
- **Later:** more parts each version.
- **Out:** synergy tags, fixed (non-modular) towers, and changing a tower's chassis or weapon.

## Acceptance (what the tests check)

- **Assembly:**
  - a light chassis rejects heavy weapons, and a heavy chassis takes both;
  - the footprint comes from the chassis;
  - stats are combined from the three parts;
  - the cost is the sum of the parts.
- **Starting kit:** the pool at run start holds a legal light tower.
- **Module swap:**
  - XP and level are kept;
  - the new module is at Mk I;
  - the refund is the right share.
- **Selling:** the refund is the right share of parts plus Mk spend.
- **Evolution:**
  - XP equals damage dealt;
  - each situation feeds the right counter, and shared situations split equally;
  - the biggest counter wins and the counters reset;
  - the cap stops XP;
  - stats only, with no behaviour change.
- **Mk:**
  - three steps on the weapon and the module;
  - the Mk IV behaviour hook fires;
  - the chassis can't be upgraded.
- Everything is deterministic and in `src/core`.

## Open questions

1. The 8 v0.1 parts themselves, including which weapons are light or heavy and what goes in the
   starting kit. These come from the pitch-and-pick rounds.

Everything else from the first draft was settled with the owner on 2026-11-18. The remaining
numbers are **(start)** values to tune in the arena.
