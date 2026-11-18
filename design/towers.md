# Tower system

Status: **draft for the owner's review** (issue #3). Decided in the question rounds of 2026-11-18.
Anything marked **Proposed** wasn't decided: it's a starting value or rule for the owner to
confirm, and it's listed under Open questions.

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
| **Weapon** | How it hurts (examples: beam, shell, arc, pulse). It needs a mount size and is either **direct fire** or **lobbed** (line of sight, [world.md](world.md)) |
| **Module** | The twist (examples: chains, slows, splits, pierces, overcharges). It fits any chassis |

- **Mount sizes:** a light chassis takes light weapons and a heavy chassis takes heavy weapons.
  The size is also the footprint: light is 2 x 2 cells (24 px) and heavy is 4 x 4 cells (48 px)
  on the 12 px grid. Whether a heavy chassis can also take a light weapon is open.
- The examples are placeholders. The real parts come from pitch and pick (see Designing parts).

### Getting towers in a run

- **The draft unlocks parts.** Between waves you draft a chassis, a weapon or a module into your
  run's **part pool**. Unlocked parts are reusable.
- **Gold builds towers.** You build any legal combination of the parts in your pool, wherever
  there's legal ground ([world.md](world.md)).
- **Proposed:** a tower's cost is the sum of its three parts' costs.

### After a tower is built

- **The chassis and weapon are permanent.**
- **The module can be swapped** for another one in your pool, for a gold cost.
  - The tower **keeps its XP and level**.
  - The new module starts at Mk I.
  - The gold spent on the old module's Mk upgrades is **partly refunded** (**Proposed:** 50%).

### Evolution (stats only)

- **XP = damage dealt.**
- **The stat that grows follows how the damage was dealt.** For example:
  - damage to armoured targets raises penetration;
  - damage at long range raises range;
  - damage to many targets at once raises effect strength.
  - **Proposed:** each kind of damage feeds a "lean" counter, and each level-up raises the stat
    with the biggest lean.
- A meter on the tower shows which way it's leaning.
- Evolution never changes behaviour: that's the parts' job.
- **The chassis sets the level cap.** For example, a light chassis caps early and a heavy one goes
  higher.

### Gold Mk upgrades (per tower)

- Gold upgrades **one placed tower's weapon or module**. The chassis isn't upgraded.
- Each weapon and each module has **three steps**:

| Step | What it is |
|---|---|
| Mk II | A small, simple change (e.g. a stat bump) |
| Mk III | A small, simple change |
| **Mk IV** | The **one major change**: a new look, stat changes **and** a new behaviour |

- Mk II and III don't need to change the sprite, and names don't need to share a vocabulary.
  Bastion Line's visible-rebuild rule doesn't apply here.

### Designing parts

1. **Pitch:** the owner describes a role or a feeling ("punishes fast enemies", "a 1950s
   raygun").
2. **Pitch page:** Claude makes a short page with **3 variants**: a sprite, a one-line role and a
   few numbers each. The owner picks one.
3. **Arena:** the pick goes into the test arena as data and is tuned by feel with the live sliders.
   This needs the arena, which comes first in v0.1 (see [game.md](game.md)).

## Interactions

- **World ([world.md](world.md)):**
  - footprints on the 12 px grid;
  - high ground gives range and fires over rocks;
  - lobbed weapons arc over rocks;
  - cliffs block everything.
- **Economy:** gold pays for building, module swaps and Mk upgrades. Income and prices are open.
- **Draft:** the draft offers parts. How many choices it offers, and how often, belong to the
  run-loop design.
- **Live tuning:** every number on a part (stats, costs, lean rules, level caps) lives in the
  hot-reloaded data file. Nothing is hard-coded.

## UI

- **Build menu:** pick a chassis, a weapon and a module from the pool. A preview shows the
  footprint, the range (with the high-ground bonus) and the total cost.
- **Tower panel:**
  - the three parts with their Mk levels;
  - level and XP, the lean meter and the cap;
  - Mk upgrade buttons (weapon, module);
  - Swap module.

## Edge cases

- **A weapon that doesn't fit the chassis's mount size** can't be picked for it. The menu greys it
  out.
- **Swapping to the same module** isn't offered.
- **A tower at its level cap** stops levelling. **Proposed:** XP earned at the cap isn't banked.
- **The pool only has heavy weapons and only light chassis.** You can't build until the draft gives
  you a fit. **Proposed:** the draft guarantees at least one legal combination early on.

## Scope

- **v0.1:**
  - the part model;
  - building from the pool;
  - module swaps;
  - evolution;
  - Mk II–IV;
  - a first small set of parts, made by pitch and pick and tuned in the arena.
- **Later:** more parts each version.
- **Out:** synergy tags, fixed (non-modular) towers, and changing a tower's chassis or weapon.

## Acceptance (what the tests check)

- **Assembly:**
  - mount-size rules;
  - footprint from the chassis;
  - stats combined from the three parts.
- **Module swap:**
  - XP and level are kept;
  - the new module is at Mk I;
  - the refund is the right share.
- **Evolution:**
  - XP equals damage dealt;
  - the lean picks the right stat for each kind of damage;
  - the cap stops levelling;
  - stats only, with no behaviour change.
- **Mk:**
  - three steps on the weapon and the module;
  - the Mk IV behaviour hook fires;
  - the chassis can't be upgraded.
- Everything is deterministic and in `src/core`.

## Open questions

1. Which stats exist, and the exact lean rules, i.e. which kind of damage feeds which stat
   (Proposed: biggest lean wins).
2. The XP curve and each chassis's level cap.
3. Costs:
   - parts (Proposed: tower cost = sum of parts);
   - module swaps;
   - Mk steps;
   - the refund share (Proposed 50%).
4. Selling towers: is it allowed, and what's refunded?
5. What happens to XP earned at the cap (Proposed: not banked).
6. Does the draft guarantee a buildable combination (Proposed: yes, early on)?
7. Sprites: does the game assemble a tower's sprite from its part sprites, or is every
   combination drawn?
8. How many parts v0.1 starts with.
9. Can a heavy chassis also mount a light weapon?
