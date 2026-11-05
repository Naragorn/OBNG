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

## Hand-to-hand damage, the victim's reaction, block training (read 2026-11-03)

Found while making OBVR's VR strikes proper hits with their own skill (OBVR
`src/game/StrikeRules.cpp`, `docs/physical-combat-spec.md` section 13.1).
Read with `dumpbin /disasm` on the Steam EXE.

- **Read:** the hand-to-hand branch of `0x005FEBF0` reads the attacker's hand to hand
  (vtable +0x284 with 0x11, an integer) and calls `0x00547280` at `0x005FF419` - cdecl, seven
  dwords: the skill's value first, then Luck, Strength, fatigue and two out pointers it writes
  the health and fatigue damage through. It returns nothing (`ret` with an empty FPU stack); the
  caller pops `1Ch`. The skill and Luck go to `0x00547B90` first.
- **Read:** the power attack's factor is `0x00546BA0` (cdecl: the skill's value, the attack
  direction; st0) at `0x005FF44D`, on the same hand-to-hand value. It looks up the mastery level
  of the skill (`0x0056A300`) and a per-direction GMST.
- **Read:** every landed hit ends at `0x006005B1`: `call [vtable+0x3A8]` on the target with
  (attacker, 0). The slot holds `0x005FE380` in the PlayerCharacter, Character and Creature tables.
  It weighs the target's Aggression (actor value 0x21) and disposition (`0x00546190`) and makes
  it hostile or reports the assault - the crime of a hit lives here. Four jumps reach the call's
  first byte (`0x005FF1BE`, `0x005FF1E1`, `0x00600581`, `0x006005A2`), so the blocked and the
  unblocked hit both end there.
- **Read:** Block is trained by the block handler `0x005F5B70` (the attacker's vtable +0x3B4,
  called from the hit handler with the blocked share): `UseSkill(0xF)` on the defender when the
  share is above 0 or the defender has no weapon. The hit handler itself only trains the
  defender's armour skill (`UseSkill(0x12)` heavy or `0x1B` light, the `0x39C` call in the
  decompile).
- **Measured (OBVR harness `strike-skills-hands.txt`, 2026-11-03):** a fist blow with the
  reaction call skipped took the beggar's health 39.0 to 35.8 and trained hand to hand; the
  slap's blow with the call made 35.8 to 32.6. **Unverified:** that a skipped call leaves no
  hostility or bounty behind (the beggar was frozen by `tai`; not looked at).

### What a modern engine does instead (derived)

- Damage, skill and the social reaction are one function in the EXE, chosen by the attack's
  type. A modern combat system keeps them as separate stages of a hit event: the damage model
  (which skill and which formula), the progression (which skill gains), the AI/crime response
  (whether the victim perceives an attack). Each is replaceable on its own; OBVR had to reroute
  three call sites inside one function to get there.

### Which stagger animation plays (read 2026-11-05)

- **Read:** the stagger start `0x005F4FD0` takes only the actor. It asks `0x005E5690(actor,
  0x1F, 0, 0)` for the group code (group 0x1F plus the weapon-type part), then plays it
  through `0x00477B60(animData, code, 1, -1)`. No direction, side or attacker goes in.
- **Read (vanilla `Oblivion - Meshes.bsa`):** one stagger file per stance and no
  left/right pair: `onehandstagger.kf`, `twohandstagger.kf`, `handtohandstagger.kf`,
  `staffstagger.kf`, `stagger.kf`, `swimstagger.kf`, `swimhandtohandstagger.kf`.
- **Read:** the sequence is looked up in `ActorAnimData+0x9C`, a
  `NiTPointerMap<UInt16, AnimSequenceBase*>` (`0x00476260` -> `0x00470960`), and fetched
  by the entry's vtable slot +0x10 with index -1 (`0x0047628D`).
  - `AnimSequenceSingle` vtable `0x00A3C72C`; `AnimSequenceMultiple` vtable `0x00A3C758`
    (both from the RTTI).
  - `AnimSequenceMultiple` slot +0x10 is `0x00470BF0`: index 0xFF draws a random number
    (`0x0047DF80`) modulo the count, any other index in range picks that entry. So the
    engine already holds several files per group and picks one at random; a caller that
    passes an index picks a given one.
  - The next function, `0x00470C40`, looks an entry up by its file name (`strrchr '\\'`,
    then a case-blind compare) - a name-based pick, unverified as a vtable slot.
- **Prior art (read):** Blockhead appends `.kf` names to the NPC base form's
  `TESAnimation` list and reloads with `0x004E3490` (its hook at `0x004E34AB`,
  https://github.com/shadeMe/Blockhead/blob/master/AnimationOverride.cpp). A Blockhead
  user reports that several variants of one group (`_1`, `_2`) are picked at random
  (http://tbsk.blog.shinobi.jp/Page/7/), which matches the `AnimSequenceMultiple` read.
- **Unverified:** which side the vanilla stagger animations lean to (a player's
  observation says left), and whether a loose extra `.kf` in `_male` (not SpecialAnims)
  joins the group without the base-form list.
- **Measured (2026-11-05, the B-spline tracks evaluated on the vanilla skeleton):** the
  one-handed, two-handed and staff staggers move the head 11-17 units to the actor's right
  (+x of the skeleton root) while the body steps about 100 units back; the fists' stagger
  leans about 10 units to its left.
- **Measured (OBVR harness stagger-sides.txt, 2026-11-05 10:08):** with a mirrored
  `OneHandStagger_OBVRMirror.kf` added to the NPC's special animations, the group's
  AnimSequenceMultiple holds two entries, `[0] onehandstagger.kf, [1]
  OneHandStagger_OBVRMirror.kf`; each sequence's +8 is its file path. Picking index 1 for a
  blow to the left and 0 for one to the right through the patched call at `0x0047628A` played
  without a fault. An unarmed, idle beggar never staggered: there is no stagger file for that
  stance (NPCs lack a plain `stagger.kf`; https://www.nexusmods.com/oblivion/mods/44846).
