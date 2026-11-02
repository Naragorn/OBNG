# Hit reactions: stagger, knockback, knockdown

How a melee hit decides whether its target staggers, is pushed back or falls.
Found while tuning OBVR's blunt blows (2026-11-02). Marked: **read** (in the
EXE), **derived**, **unverified**.

## What the EXE does (read)

The hit handler is `0x005FEBF0`. Decompiled: `../re/export/rea-all/functions/0x5febf0.c`.

### "Knockdown" chance, which is really the stagger

- **Read:** `0x005475D0` takes (the target's Agility, Luck, fatigue ratio, damage).
  - It returns a roll of `rand() % 100 / 100 < p`, where:

    ```
    p = (damage * fKnockdownDamageMult + fKnockdownDamageBase)
        / (fKnockdownAgilMult * A * F + fKnockdownAgilBase)
    p = min(p, fKnockdownChance)
    ```

  - The terms:
    - `A = clamp(Agility + Luck * fActorLuckSkillMult + iActorLuckSkillBase, 0, 100)` (`0x00547B90`).
    - `F = fFatigueBase - (1 - fatigueRatio) * fFatigueMult` (`0x00547F00`).
  - GMST slots:
    - Read in the EXE's setting constructors: `fKnockdownDamageMult` `0x00B36F98` and `fKnockdownChance` `0x00B36FA0`.
    - **Derived** from their neighbour order: `fKnockdownAgilBase` `0x00B36F80`, `fKnockdownAgilMult` `0x00B36F88` and `fKnockdownDamageBase` `0x00B36F90`.
- **Read:** the handler calls it at `LAB_0060024f`. A true roll leads to the "%s is staggered down by %s's %.1f total point blow!" debug line and to the stagger start `0x005F4FD0`, which plays anim group 0x1F with action 8.
  - The CS wiki agrees: "'Knockdown' refers to what is more commonly known as 'staggering'", and `fKnockdownChance` defaults to 0.3. Source: https://cs.uesp.net/wiki/FKnockdownChance
- **Read:** a second stagger route uses the damage as a fraction of the target's health (actor value 8) against a setting, or a random draw. The setting's name was not read.
- **Unverified:** the weapon type plays no part in either route as far as the decompile shows. Blunt and blade stagger alike.

### Knockback (read)

- `0x00547690` computes the push:

  ```
  push = (damage * fKnockbackDamageMult + fKnockbackDamageBase)
         * (fKnockbackAgilMult * A / F + fKnockbackAgilBase)
  ```

  - `A` and `F` are as above.
  - The GMST slots are `0x00B36FC0`, `0x00B36FB8`, `0x00B36FB0` and `0x00B36FA8`. The `AgilBase` name is derived.
- The push is applied along the line from attacker to target, through the character proxy. The details are in OBVR `docs/combat-comfort-spec.md` §2.
- CS wiki defaults: `fKnockbackDamageMult` 10 and `fKnockbackAgilBase` 1.

### A real fall (ragdoll)

- **Read:** the hit handler throws the target only through a perk route.
  - The attacker's mastery must be above level 2 (`0x005F23B0`).
  - The roll is `iPerkMarksmanKnockdownChance` (`0x00B37240`).
  - The fall is the actor's vtable `+0x2F0`, with a force from `0x00547770(50)`, which maps through the `fDeathForce*` settings.
  - The same block rolls `iPerkMarksmanParalyzeChance` (`0x00B37248`).
- UESP lists the blunt and blade perks:
  - Blunt Expert: backward power attack, 5 % knockdown.
  - Blunt Master: forward power attack, 5 % paralyze.
  - Source: https://en.uesp.net/wiki/Oblivion:Blunt
  - **Unverified:** which code rolls the melee perks' knockdown.
- No speed and no weapon weight enter any of these rolls. Vanilla has no "hard blow throws".

## What a modern engine does instead (derived)

- A physical reaction comes from the impulse: mass × velocity at the contact point against the victim's balance (stance, mass).
  - A random roll gated by Agility does not do this.
- Blunt and edged hits differ in how the impulse passes through: blunt is all impulse, edged partly cut.
- OBVR does this with speed thresholds:
  - Pommel: falls at 4 m/s.
  - Half-sword hilt: falls at 7 m/s.
  - Torch: staggers only.
