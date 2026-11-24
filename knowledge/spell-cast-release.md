# When the player's spell is released, and when its magicka is taken

Read for OBVR's charged spells (2026-11-22, Ghidra 12.1.4 via REA, Steam GOTY build,
Oblivion.exe sha256 a8f31384...). All addresses **read** in the EXE; nothing here was run.

## The cast in three steps

- `0x00699190` MagicCaster::CastMagicItem (xOBSE names it) is the cast's **start**: it
  checks the item (caster virtual +0x30 / +0x34 / +0x3C) and plays the cast animation
  through the caster's virtual +0x10 (targeted) or +0x14 (self). No projectile, no cost.
- `0x0069BEC0` is the **release** (a `__thiscall` on the MagicCaster, one stack argument,
  pushed 0 by the engine). It reads the active item (virtual +0x30), asks virtual +0x1C
  whether it can be used (the magicka check), and then:
  - usable: caster state `[caster+8] = 3`, calls `0x0069AF30` (the apply: makes the
    effects, and for a target spell the projectile through the factory `0x0069A060`, see
    spell-projectile-origin.md), then **virtual +0x18 with (item, 1): the cost is taken
    after the spell has gone**; with `caster+4` set, `0x0069DD50(0, 0.75)`.
  - not usable: state 7, the "not enough" message (`0x0065A8B0` with the item's text),
    virtual +0x18 with the item alone, the item cleared (+0x34(0), +0x3C(0)).
- The player's release is called from the animation update `0x005FCAB0` (the function
  OBVR already wraps for the aim at the source), at **two call sites**, each
  `push 0; mov ecx, edi (actor+0x5C); call 0x0069BEC0`:
  - `0x005FCE1D` (after the caster's virtual +0x30 answered an item), and
  - `0x005FCF6E` (the other branch of the same switch).
  Other callers of 0x0069BEC0: `0x005E44C0` and others in 0x005E4xxx, `0x0060B7C0..`,
  `0x0060BDB2..` (not read: NPC or script routes, probably).

## The player's MagicCaster table (0x00A739BC), slots as bytes read

+0x00 0065D480, +0x04 0065D490, +0x08 0069BE90, +0x0C 0065D4E0, +0x10 005EA2E0,
+0x14 005F3E00, +0x18 005FC890, +0x1C 0065F720, +0x20 005E1860, +0x24 005EDC40 (magic
node), +0x28 006992A0, +0x2C 0065DAF0, +0x30 0066B130, +0x34 0065DE80, +0x38 0066B140,
+0x3C 0065DE90, +0x40 00699090. Names after xOBSE's declaration order (AddAbility,
AddDisease, AddObjectEnchantment, FindTouchTarget, PlayTargettedCastAnim,
PlayCastingAnim, ApplyMagicItemCost, ...) fit +0x10/+0x14 (the two animations) and
+0x18 (the cost, called with (item, 1) after the apply) - a fit, not a second source.

## Offsets from xOBSE (llde/xOBSE, via DeepWiki)

EffectItem magnitude +0x04; MagicItem's effect list +0x0C; SpellItem magickaCost +0x3C;
ActiveEffect magnitude (float) +0x18, its EffectItem +0x0C; MagicCaster magicNode +0x04,
state +0x08.

## What this means for a held spell (derived, not run)

The release is one call in the animation update with the caster in `ecx`: hold it back
there while the player holds the cast, and make the same call when they let go. The
magicka is taken inside that call, after the spell has gone, so holding costs nothing.
Open: what the cast state does when the animation ends without its release (state stays
before 3?), and where a per-cast strength can go - the EffectItem's magnitude is the
spell form's (shared, read again at the hit), the ActiveEffect's is made at the hit.
