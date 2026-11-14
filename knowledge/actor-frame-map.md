# The actor's frame: what each part reads and writes (2026-11-14)

The synthesis of the slice `actor-high-update` (manifest `re/slices/actor-high-update/manifest.json`) for step 2 of
`docs/subsystems/actor-frame-parallel.md`: per function what it reads and writes of (a) its own actor/object,
(b) other actors, (c) the Havok world, (d) globals and shared lists, (e) the heap - to tell the parts that may run
for several actors at once from those that may not. Map and refute only; nothing was rebuilt, run or measured for
this file. Every fact below is taken from the function files `re/functions/0x<addr>.md`; nothing is added to them.

Marks (AGENTS.md): **R** read in the instructions (address given), **D** derived (from what is named), **U**
unverified, **Up** unverified, seen only in callee pseudocode (`re/export/rea-all`, a reading aid). **M** measured
appears only where a file quotes a run. A callee not followed is an unknown, not a clearance. Names are proposals.

## 1. The functions and their state

| address | proposed name (seed name) | state |
|---|---|---|
| 0x00677EC0 | `ProcessManager::UpdateHighActors` (ProcessManager high-actors loop) | mapped |
| 0x00605770 | `Actor::UpdateHigh` (Actor high update) | mapped |
| 0x00603CA0 | `Actor::Update` | mapped |
| 0x005FAAE0 | `Actor::UpdateVitals` (Actor vtable slot 0x368) | mapped |
| 0x00471C00 | `ActorAnimData::UpdateControllersOrBound` (ActorAnimData step before ApplyAnimData) | mapped |
| 0x005F6540 | `Actor::CalculateDetectionLevel` (name unknown) | mapped |
| 0x0063A210 | `HighProcess::UpdatePackage` (HighProcess vtable slot 4) | mapped |
| 0x00603500 | `Actor::UpdateHeadTracking` (Actor animation step) | mapped |
| 0x00896000 | `bhkCharacterController::Update` (Character controller update); class U | **disputed** |
| 0x00891780 | `CharacterController::UpdateProbePoints` (Controller support probes) | mapped |
| 0x008AE890 | `CharacterProxy::CheckSupport` (Character proxy support check) | mapped |
| 0x008AE100 | `CharacterProxy::CheckSupportWithCollector` (Under the proxy's support check) | mapped |
| 0x008AD7D0 | `CharacterProxy::Integrate` (Character proxy integrate) | mapped |
| 0x008ECC60 | `ShapePhantom::SetPositionAndLinearCast` (Phantom linear cast) | mapped |

"Mapped" = held against three refuters; five of the 13 held only after a fix round. Which five is not recorded in
the files; corrections are written into 0x00677EC0, 0x00471C00, 0x005F6540, 0x00603CA0 and 0x00896000.

**0x00896000 is disputed.** After its fix round one refuter still found a wrong branch label: row B54 reads the
stack byte [ESP+0x12] tested at 0x00896E25 as "not smallStep", but that byte is overwritten at 0x00896764 and in the
substep loop, so at 0x00896E25 it means "the last substep ran the integrate". (An earlier round found the still-reset
condition B7/B8 inverted; the fix corrected that.) The B54 branch only picks a debug colour. Its read/write facts
are used below, marked "(disputed fn)" in sections 2-3 and "Ctl" (disputed) in section 4.

## 2. Call structure of one frame's actor work, as the maps give it

```
0x00678510 ProcessManager::Update (knowledge/threading.md)
 `- 4th call: 0x00677EC0(0, 0), this = 0x00B3BD00 (D: high list at +0x68 matches OBVR GameAddresses.h)
     once: [0x00B3B935] = 0 (R 0x00677ECD); count high list -> this+0xA8 (R 0x00677F05)
     per high actor, list order head -> tail via cursor this+0x74/+0x78 (R, B43-B46):
       filters (flags bits 5/11/21, process, level) -> 0x004D7E90 distance to player -> IsActor vt+0x190
       either 0x005F1330 branch: process vt+0x18(actor,0), process+0x1D1 = 1, vt+0x20; no update (R C8)
       or     0x0065A2C0 / vt+0x154 / vt+0x148, then actor vt+0x1C0(arg1 = 0.0) (R 0x00678004)
              -> 0x00605770 (U: slot resolution rests on docs/performance-backlog.md RTTI reading)
       if player+0x590 > 0: 0x005F2530, 0x005F25F0, 0x005F2720 with 2.0 (R C6)
       level re-check -> cursor reset (R C9)
       stagger block: vt+0x1D8 -> vt+0x1DC or vt+0x1D4(stagger); 0x00674820 adds to 0x00B3BD00+0x60 (R C11, C12)
       extra-0x23 block: other actors' vt+0x1C0(3600.0 when arg1 <= 0) inside this actor's turn (R C14; D C15)
     once at the end: 0x00677500(frame seconds) - may demote high actors (Up C19)
0x00605770 (dt)  [R order of the call sites]
   B1 flags bit 11 -> return
   short path (byte this+0x78 == 0 and 0x0045A500 == 0): process vt+0x20, 0x006A2280 magic tick, this+0xBC := t
   long path:
     1 day check (L1-L6), 0x005F0410 (L7-L8, not followed)
     2 AI: 0x005F8000, 0x005F7CF0, then process slot 4 (+0x10, = 0x0063A210 per backlog, U) when dt == 0.0 or
       player, else slot 3 (+0x0C, dt) (R 0x006059DA-0x00605A09); or the dead branch D1-D10 (0x006748B0 respawn)
     3 actor vt+0x368 = 0x005FAAE0([0x00B3BCF0]) at 0x00605B7F (R)
     4 one-shot 3D reset R1-R9: 0x008AB440 (Havok state) or 0x00476D10 + 0x00474510 (0x00471C00 + 0x00471F20)
     5 Actor::Update 0x00603CA0(dt) at 0x00605D31 unless vt+0x388's object +0x18C == 4 (R)
     6 cell re-home 0x004DD4B0 (C1-C5)
0x00603CA0 (dt)  [R call sites in order]
   preamble getters; controller pre-step 0x005E14E0 (controller+0x1F0); process +0x2F4/+0x440/+0x60; mount handling
   [0x00B3CBD0] = 1 if not mounted and vt+0x388() != 0 (0x0060407E)
   root node local transform written (0x006040CD-0x00604227); light rescan 0x007C6FF0 (0x00604247)
   0x00471C00 (0x00604257) -> mount spine twist -> 0x00603500(frame seconds) (0x006043D5, gate B28)
   -> ApplyAnimData 0x00471F20 (0x006043DC) -> root-motion correction -> [0x00B3CBD0] = 0 (0x006044AE)
   unless first-person player: water/breath/splash, detection 0x005F6540 (0x00604F68), greeting, process timers
   tail: process +0x318, +0x424/+0x42C, 0x00633250, 0x005F12D0, fade value -> 0x007ABD00
0x00603500: ... detection 0x005F6540 again (0x00603B01, gates #42/#43) ... solve, spine twist
0x00896000 (param*): caller UNKNOWN - no static caller (R meta.json), not called by 0x00603CA0 directly
   (R 0x00603CA0 C5); its place in this order is not established by the maps
   per substep: 0x00891780 (0x008968A5) -> 0x008AE890 (0x008968C9) -> 0x008AE100 -> 0x008AD070 (not followed)
                -> state vt+0x18 -> 0x008902B0 -> 0x008AD7D0 -> phantom vt+0x30 (= 0x008ECC60, U) 1-2x per
                iteration, at most [0x00B2EFB8] iterations; 0x008ABAC0 at the integrate's end
```

**Within one actor's turn - a later step reads what an earlier step wrote:**

| writer (site) | state | reader (site) | mark |
|---|---|---|---|
| 0x00605770 | passes [0x00B3BCF0] as `now` | 0x005FAAE0 | R 0x00605B6E |
| 0x00605770 short path / 0x005FAAE0 | actor+0xBC stamp (same idiom, both write it) | 0x005FAAE0 next call | R 0x00605831, 0x005FAE83 |
| 0x0063A210 case 0x0D, 0x00603500 #26 | process movement flags via 0x005E05F0(0x30) | 0x005FAAE0 (process vt+0x2C0) | D (writers R call sites; vitals runs after the AI slot, R order) |
| 0x00603CA0 | [0x00B3CBD0] = 1 (0x0060407E) | 0x00471C00 clears it (0x00471E1C) before the caller's own clear | R writes; whether 0x0060407E reaches 0x00604257: U |
| 0x00471C00 | actor+0xC4 masks 0x1/0x2/0x4 (0x00471CE1, 0x00471D0C, 0x00471D56, 0x00471E5E) | 0x00603500 gate: bits 0..2 all set (0x00603586-0x00603595) | R both; dependency D |
| 0x00471C00 | anim data dirty word = 0, controllers run (0x0047CA30) | ApplyAnimData 0x00471F20 on the same anim data | R (same EBX, 0x00603CA0); what 0x0047C740 writes U |
| 0x00891780 | controller +0x380..+0x39F | not found in the slice (0x00891780 open 4) | R write; reader U |
| 0x008AE100 | out record controller+0x260 (+0x00, +0x10, +0x20) | 0x00896000 at 0x008968DF | R both (disputed fn) |
| 0x008AD7D0 | proxy +0x10 velocity, +0x20 displacement, manifold +0x78 | 0x00896000 (proxy velocity, manifold count 0x00896ADB) | R (disputed fn) |
| 0x00603CA0 | controller(0x0065A2C0)+0x2A0 := 0/5, +0x338 | 0x00896000 writes +0x2A0 = 0xB (0x00896CDD) | R both; same object U (section 7) |

**Across actors of the same call - a later actor reads what an earlier actor's turn wrote (R unless marked):**

- The stagger argument of vt+0x1D4 grows with every earlier actor that took that path (0x00677EC0 C11).
- The manager's list 0x00B3BD00+0x60: each add scans what earlier adds wrote (0x00677EC0 C12; list semantics Up).
- The cursor: an actor that leaves the high level resets it to the previous node (C9); +0x74 is not reset per call (D C10).
- Other actors' whole updates run inside actor k's turn through extra 0x23 (C14).
- [0x00B3CBD0], [0x00B2EFB8], [0x00BA7A5C], [0x00B333C0]: written per actor, see section 4.
- [[tgt+0x58]+4] = 1: an actor's package step writes another actor's process (0x0063A210 R 0x0063B0F3); whether
  that actor's later turn reads it: U.
- Phantom positions and the broad phase: each cast moves the caster's phantom (0x008ECC60 R 0x008ECC82) and later
  casts walk overlap lists that include other characters' phantoms (knowledge/threading.md, M) - D that later
  actors' casts see earlier actors' moved phantoms; which fields the cast functions read: U.

**Cross-file derivation:** from 0x00678510 the high actors get arg1 = 0 (0x00677EC0 C15), and 0x00605770 calls
process slot 4 when dt == 0.0 (R) - so every high actor of this loop takes slot 4 0x0063A210, the extra-0x23 actors
(3600.0) slot 3 unless the player (D). This fits the measured 529 437 slot-4 calls that 0x00605770's open question
puts against slot 3 calling slot 4 (D; not settled - slot 3 0x0063BA30 unread).

## 3. The map

Per function: (a) own actor/object, (b) other actors/references, (c) Havok world, (d) globals and shared lists,
(e) heap. "nf" = not followed. Then the callees not followed.

### 0x00677EC0 ProcessManager::UpdateHighActors
- (a) this (0x00B3BD00, D): R high list +0x68 nodes; R/W cursor +0x74/+0x78; W +0xA8 (R 0x00677F05). Actor: R vtable,
  +0x08 flags, +0x58 process; vfuncs +0x148/+0x154/+0x170/+0x190/+0x198/+0x1C0/+0x1D4/+0x1D8/+0x1DC (calls R, effects
  U); extra list +0x44 via 0x00420EF0/0x00424D00/0x00420F00 (Up; GetByType moves to front under a named lock, per
  knowledge). Process: W byte +0x1D1 = 1 (R 0x00677FB6); vt+0x18/+0x20/+0x1C (nf).
- (b) R the player [0x00B333C4] (distance, +0x590 via 0x0065D550 Up). Extra-0x23 actors: R +0x58, extra 0x1F,
  process +0x08; W their whole update via vt+0x1C0 and, level 3, process vt+0x1C (R calls).
- (c) none in its own instructions (R); only inside vt+0x1C0.
- (d) W [0x00B3B935] = 0 (R 0x00677ECD). R [0x00B333C4]; R [0x00B33B00] via 0x0045A500 (+0x18; also
  [0x00B33398]+0x10 thread id, Up); R setting 0x00B36CD8 via 0x00403C00; R/W 0x00B3BD00+0x60 via 0x00674820 (call R,
  list add Up); R [0x00B33E9C]; R g_TimeGlobals 0x00B332E0 via 0x00402BD0; +0xAC via 0x00677500 (Up).
- (e) FormHeap_Allocate 0x00401F00 (8 B) x2 at 0x00678195/0x006781B4 per actor with extra 0x23 (not on the 0x005F1330
  branch), plus nodes in 0x0067B1E0/0x00446CB0 (Up); frees 0x00401F20 x2, 0x004526E0 x2 (R calls).
- Callees nf: vt+0x190, +0x154, +0x148, +0x1C0, +0x170, +0x1D8, +0x1D4, +0x1DC, +0x198; process vt+0x18, +0x20, +0x1C;
  0x005F2530, 0x005F25F0, 0x005F2720; 0x00446CB0. Pseudocode only (Up): 0x007616D0, 0x00659A00, 0x004D7E90,
  0x005F1330, 0x0065A2C0, 0x0065D550, 0x0045A500, 0x005E0DC0, 0x00403C00, 0x00674820, 0x00420EF0, 0x0041FB20,
  0x0067B1E0, 0x005E03A0, 0x00402BD0, 0x00424D00, 0x004526E0, 0x0046CB60, 0x00420F00, 0x00677500.

### 0x00605770 Actor::UpdateHigh
- (a) R +0x08, +0x3C, +0x40, +0x58, +0x78, +0x84, +0xB0, +0xBC, +0xC0, +0xCC, +0xFD. W +0xBC (0x006057DF, 0x00605803,
  0x00605831), +0x84 (0x00605A58), +0xC0 = 0 (0x00605CFF), +0xFD (0x006058AB, 0x006058B4) (R). W [seq+0x48] :=
  [seq+0x30] in the sequence object of 0x004706E0 (R 0x00605CD9, one-shot path; per-actor or shared U). Extra list:
  GetByType(0x44), RemoveExtra(0x59), 0x00420050 prunes type 0x21 (Up). Process vt+0x08/+0x0C/+0x10/+0x20/+0x34/+0x4F0
  (nf). MagicTarget +0x68 via 0x006A2280 may remove an effect and free (Up).
- (b) player pointer compared only (R 0x006059EB, 0x00605D36). AI slots, 0x005F8000, 0x005F7CF0, 0x005F0410,
  0x004DD4B0, Actor::Update may touch others (U). 0x006748B0 walks 0x00B3BD00's list +0x58 (Up, dead branch only).
- (c) 0x008AB440 on the one-shot 3D-reset path: node tree's collision state, 0x008AA7F0, an object from
  0x00700010(&0x00BA7F3C) (Up; a Havok write D). Nothing else in its own body (R).
- (d) Writes no global in its own instructions (R claim 7). R [0x00B33B00]+0x18 via 0x0045A500; [0x00B33398]+0x10
  (Up); 0x00B3BD00 as ECX; [0x00B3BCF0] via 0x00673B00 (Up); 0x00B332E0 getters 0x00402BA0/0x00402C40/0x00402C00/
  0x00402BD0 (Up); [0x00B37D80], [0x00B37D88], [0x00B3BD98], [0x00B3BDA4], [0x00B14E98], [0x00B33E9C] (R 0x00605A52),
  0x00B333A0 g_TES, [0x00B35C1C] (R).
- (e) none in its own instructions (R). 0x005F8000 allocates 0x3C B (Up); 0x00420050 frees (U); 0x006A2280 deleting
  destructor (D).
- Callees nf: 0x005F0410, 0x005F7CF0, 0x005E6680, 0x00476D10, 0x005F5D10, 0x00603CA0 (own file), 0x004DD4B0; vcalls
  actor +0x1A0/+0x380/+0x388/+0x198/+0x19C/+0x20C/+0x368/+0x170/+0x174/+0x278/+0x1C8/+0x1E0/+0x164, process
  +0x08/+0x0C/+0x10/+0x20/+0x34/+0x4F0, extra object +0x184, +0x388 result +0x18C. Partly: 0x008AB440, 0x006748B0,
  0x004DC070, 0x00567770, the anim-data queries 0x00470D00/0x00470FC0/0x00474AB0/0x004706E0.

### 0x00603CA0 Actor::Update
- (a) W this+0xD8 (0x00604589), this+0xE4 := player (0x006050C9); root node local rotation +0x30..+0x50 (0x00604227)
  and translate +0x54..+0x5C (0x006040CD-0x006040E3, 0x00604192-0x00604198); Bip01 Spine/Spine1 rotations (B27,
  mounted); controller (0x0065A2C0) +0x2A0 (0x00604929, 0x00604BDE, 0x00604C1D, 0x00604C48), +0x338 (0x006048B4);
  anim data +0x18..+0x20 (R). Through own-object callees (nf): SetPos 0x004D8A30, 0x004D8A60, flags
  0x005E05F0/0x005E0610, AV vt+0x2A4, damage vt+0x220, heading vt+0x1E8, unequip 0x005F2E70 and more. ~30 process
  vslots (nf).
- (b) R player [0x00B333C4] +0x588, +0x5D4, +0x600; calls on the player 0x00660130 (only when this is the player),
  0x005E0550, vt+0x354, GetDistance, 0x006605A0(0), 0x004C9BE0 (R sites). R the mount's heading, position, controller
  +0x32C (R). Hands the player to process +0x484/+0x1A4 (greeting; writes nf).
- (c) no direct Havok call (R); controller fields read/written; 0x008907A0(&{0,0,+-5}, 1.0) on it (nf). Does not call
  0x00896000 (R C5).
- (d) W [0x00B3CBD0] := 1 (0x0060407E, conditional), := 0 (0x006044AE, always on the animation path) (R). W
  [0x00B3B77D] (player only, 0x006047D2, 0x00604845) (R). R: [0x00B333C4], [0x00B33E9C] x5, 0x00B332E0,
  [0x00B14E90], [0x00B3BAA8], [0x00B1490C], [0x00B333B8], [0x00B3F9A8..B0], [0x00B258DC..E4], [0x00B37B38], settings
  0x00B37D10/0x00B37D70/0x00B37B40/0x00B36AD8, [0x00B33398]+0x24, [0x00B3BB10]/[0x00B3BB0C], [0x00B258E8],
  [0x00B0762C], [0x00B075F0], 0x00B35288. Through callees (R sites, writes U): light list 0x007B4280(0) ->
  0x007C6FF0; temp-effect list 0x00678D30 on 0x00B3BD00 (splash); sound manager 0x006ADE50/0x0065A970/0x006B7xxx;
  interface menu 0x415 (player only); accumulator 0x007B7650/0x007ABD00.
- (e) FormHeap_Allocate(0x20) at 0x00604B22 (splash); FormHeap_Free at 0x006046A0, 0x00604836, 0x00604A98 (R);
  0x0065A970/0x006ADE50 allocate (D); smart-pointer releases 0x007016A0 (U).
- Callees nf: all 107 direct callees except the slice's own (0x00471C00, 0x00603500, 0x005F6540) and the named
  helpers; in particular process vtable 0x00A71814 slots +0x2F4/+0x440/+0x318/+0x424/+0x18C/+0x474/+0x484/+0x1A4/
  +0x158, 0x005E14E0, 0x0088D370, 0x008907A0, 0x007C6FF0, 0x00678D30, 0x007ABD00, 0x005F12D0, 0x00633250, 0x004723A0.

### 0x005FAAE0 Actor::UpdateVitals
- (a) R/W this+0xBC (W 0x005FAB16, 0x005FAB3A, 0x005FAE83) (R). R +0x58, +0x5C, passes +0x44 (R). Own fatigue via
  0x005E07D0 (0x005FACBF, 0x005FAD16) and magicka via vt+0x2A4(9,...) (0x005FAE39) (call R, write D from OBVR).
  0x005F2720/0x005F2530 regen via +0x2A4, 0x005E7A60 list +0x9C (Up).
- (b) R [0x00B333C4] (0x005FAB82, 0x005FACC8, 0x005FAD3F, 0x005FAE4E), player +0x6E5 (0x005FAB88), +0x594
  (0x005FAE53) (R); +0x590 via 0x0065D550 (Up). No write to another actor in its own instructions (R).
- (c) none in its own instructions (R); unfollowed virtuals U.
- (d) writes no global (R). R settings 0x00B375F0, 0x00B375F8, 0x00B37600, 0x00B37608, 0x00B37610, 0x00B37010 (R);
  callees read 0x00B37570, 0x00B37558, 0x00B37550, 0x00B37F38, 0x00B37F30, 0x00BAABE0 (Up). 0x00403C00's write to
  0x00B35464 happens only with ECX == 0, never from here (D).
- (e) none among its 14 direct targets (R). 0x005E7A60: FormHeap 0x00401F00 (8 B) and 0x00401F20 every call (Up).
- Callees nf: 0x005F1910, 0x0056A300, 0x005E07D0, 0x00403C00, 0x0065D270, 0x00420E90 -> 0x0042AFF0; vslots actor
  +0x198/+0x2E4/+0x330/+0x33C/+0x2D8, MagicCaster +0x30, process +0x2C0/+0x2D0/+0x468. Up only: 0x00547ED0,
  0x00547F40, 0x00548D60, 0x009828C0, 0x0065D550, 0x005F2720, 0x005F2530, 0x005E7A60.

### 0x00471C00 ActorAnimData::UpdateControllersOrBound
- (a) anim data: R +0x00/+0x04/+0x08/+0x94, W +0x00 = 0 (0x00471E40, 0x00471EFA). Root node [this+0x04]: W world
  bound +0x20..+0x2C on the hidden path (0x00471EEB-0x00471EF7); controllers run by 0x0047CA30 (writes U). Actor: R
  +0x0C, +0xB0; RMW +0xC4 masks 0x1/0x2/0x4 via 0x005E1370 (R). vcalls +0x154/+0x170/+0x174/+0x27C/+0x334/+0x380/
  +0x388/+0x168 (nf).
- (b) A/B from vt+0x380/vt+0x388 may be another object (D). Object L and owner L+0x1C: detached vt+0x88 and
  reattached vt+0x84 around the controller run (R calls; owner U - a (d) write if a shared light list).
- (c) none in body (R).
- (d) W [0x00B3CBD0] = 1 (0x00471DFD, only A != 0), = 0 (0x00471E1C) (R); read by 0x006CBEE0 (interpolator, Up). R
  [0x00B333C4], [0x00B333CC]+0xDC (camera), 0x00B35288, [0x00B30AAC]. Via callees (Up): 0x0047F7B0 R/W frustum
  plane cache 0x00B34398, stamp 0x00B069C4 (vs 0x00B33EA0), static-init bit 0x00B343FC; 0x007B7650 R/W singleton
  pointer 0x00B430FC; 0x007AA3C0 W singleton +0x223C/+0x2240 cache.
- (e) NiPointer assign 0x0075FA90 / release 0x007016A0 on L and tmp - a free when last reference (D). 0x007B7650
  allocates 0x226C B on its first call only (Up).
- Callees nf: 0x0047C740 (under 0x0047CA30), 0x0075FA90, 0x00560920, actor vcalls above, owner vt+0x88/+0x84;
  partly: 0x0047F7B0.

### 0x005F6540 Actor::CalculateDetectionLevel
- (a) R process +0x58, cache entry via process vt+0x3B0, vt+0x36C, AVs via vt+0x288/+0x284, vt+0x174, +0x40, vt+0x330.
  W the detection entry via process vt+0xA8(target, code, los, level) (R 0x005F6950; inside nf). W X+0x180 via
  0x00612810 on X = vt+0x330() when 0x006135F0(X) == target (call R, field D from Up); 0x006135F0 prunes and frees
  X's list nodes (Up).
- (b) target (both slice callers pass the player, R C13): read only - vt+0x154, AV 0x7/0x1F/0x2E/0x2F, process
  vt+0x3AC/+0x2C0/+0x35C, parent cell flags, name (R; no store through EBP). Target-side virtuals may cache (U).
  R [0x00B333B4] selected ref (+ vt+0x190).
- (c) none in body (R). Line of sight 0x005F2820 nf; its pseudocode references 0x00BA7D9C (U whether it casts).
- (d) **RMW [0x00B333C0] `ADD dword,1` without LOCK** (R 0x005F65D6). R [0x00B30AAC], [0x00B37A50], [0x00B333C4],
  [0x00B3BD9A], [0x00B3BD9C], [0x00B333B4], [0x00B12DB4], [0x00B02E24], FontManager singleton (via 0x00576A30),
  constants 0x00A2FAA8/0x00A6E688/0x00A6C820/0x00A30E48. Callees (Up): 0x005463F0 reads 0x00B366E0..0x00B36CB8;
  0x005F3B50 reads 0x00B333B8.
- (e) none in body (R; snprintf into the stack). Callees U: 0x006135F0 (0x00401F20), 0x0057B8E0, 0x005F2820.
- Callees nf: 0x004D7E90, 0x005F2820, 0x005F3B50, 0x005F1910, 0x0056A300, 0x00547B90, 0x005463F0 (purity U),
  0x00576A30, 0x009833BB; virtuals process +0x3B0/+0x3AC/+0x2C0/+0x36C/+0x35C/+0xA8, actor +0x154/+0x174/+0x284/
  +0x288/+0x330, selected +0x190. Up only: 0x006135F0, 0x00612810, 0x005E0550, 0x006ECC80, 0x005E06C0, 0x004C97F0,
  0x00404FB0, 0x004DA2A0, 0x0057B8E0, 0x009828C0.

### 0x0063A210 HighProcess::UpdatePackage
- (a) this (the HighProcess): R/W the fields of its struct table - timers +0xBC/+0x22C/+0x248/+0x28C/+0x2B0, flags,
  package +0x08 (W 0 at 0x0063B8A0), target +0x2C, +0x30, +0xC0 owned object (released vt+0x10(1)), lists +0x3C/+0x44
  (R). Actor: R +0x08, +0x2C, +0x40, +0x58; W own process through [actor+0x58] +0x08/+0x04 (0x0063B7DC, 0x0063B7F7,
  0x0063B860, 0x0063B866) (R). Extra list +0x44: 0x004FBF90 script event mark, 0x0041FFC0, 0x004246D0 (meanings U).
  ~60 process vslots (procedure handlers +0x508..+0x58C and more), nf.
- (b) **W [[tgt+0x58]+4] = 1 - another actor's process** (R 0x0063B0F3, case 0x0D, tgt = this+0x2C). Case 0x0F:
  [EAX+4] = 3 into the object from vt+0x3B0(tgt) (R 0x0063B3FE; owner U). tgt vt+0x198(1) (writes U). 0x005E7BE0
  calls process vt+0x17C(0) on listed actors (extra 0x23) with package kind 7 (Up; writes U). 0x00663A00 walks
  actors (Up). 0x00566870(pkg, tgt, 1) writes into the package (U). Mount: vt+0x228(frame seconds) (R B56), 0x005E9A60,
  0x005F80D0, 0x005F0410, 0x00602050 (nf). R player +0x588.
- (c) no direct call (R claim 12); via vcalls, 0x00602050/0x005F0410 (-> 0x0088D0E0, Up), movement 0x00603160/
  0x00603320/0x00685530: U.
- (d) writes no global (R claim 2). R [0x00B33E9C] x6, 0x00B332E0 (0x00402BD0/0x00402BA0), [0x00B333C4], [0x00B3C0AB],
  [0x00B33B00], table 0x00B152B0, 0x00B12988, [0x00B13980]/[0x00B139A4]/[0x00B1399C], [0x00B15800]
  (bUseBackgroundPathing), [0x00B3BF80], [0x00B36A80], [0x00B36C18], [0x00B35EB0], settings 0x00B37A58/0x00B36C88/
  0x00B36B40/0x00B36AA0/0x00B36A98. Written by callees: [0x00B3A6A4] (0x00571F90, when null; Up), the pathing
  object's queue (0x006826D0 on [0x00B3BF80], U), interface debug text (0x00579B60).
- (e) FormHeap_Free on +0x44 (0x0063B904) and each item of +0x3C (0x0063B927), only in case 0x2C's done tail (R);
  0x0065C620/0x004526E0 free (Up); 0x00571F90 allocates 0x15F0 B once (Up); 0x005F80D0 allocates (Up, when U).
- Callees nf: process vslots (+0x18, +0x20, +0x124, +0x188, +0x194, +0x36C, +0x388, +0x3C0, +0x508..+0x58C), actor
  vslots, 0x005EB400, 0x00635E20, 0x0063A020, 0x00693210, 0x005E2E00, 0x005EAE70, 0x006347E0, 0x005E4260,
  0x00603160, 0x00603320, 0x004DE770, 0x0041FFC0, 0x005E9A60, 0x005F80D0, 0x005E7BE0, 0x005E6D70, 0x00602050,
  0x00654F10, 0x00569A10, 0x004D74B0, 0x005F0410, 0x005E4140, 0x00566870, 0x00566DC0, 0x00683D80, 0x00685530,
  0x006825C0, 0x006826D0, the procedure handlers 0x006384B0/0x0062A0E0/0x00628330/0x0062B5C0/0x00633DF0/0x00630100,
  0x005FCAB0 kAttackUpdate.

### 0x00603500 Actor::UpdateHeadTracking
- (a) R/W actor +0xDC, +0xE0, +0x100; R +0x3C, +0x58, +0xB0, +0xC4 (R). Own process: vt+0x480(target) writes the look
  target, vt+0x4D8(1), +0x2C4(0x30, 0) via 0x005E05F0 (R sites; bodies nf). Head-tracking object: W +0x1D4, +0x1D5,
  +0x198 (R); vt+0x78/+0x84/+0xD0, solve 0x0054B010/0x0054B3E0 (nf). Own skeleton: UpdateWorldData vt+0x74 on
  NonAccum's grandparent, parent, NonAccum (R 0x00603BF8-0x00603C1A; writes D); spine twist 0x0047CAF0 on Spine2's
  parent (nf). Turn 0x00685530 -> actor vt+0x1E8 heading (Up).
- (b) R player's distance to this actor and to the target; R look target vt+0x11C/+0x154/+0x174/+0x188/+0x190/+0x380;
  R the package target. **0x006605A0 on the player writes the player's list at +0x5AC and frees nodes** (call R
  0x00603AD6, writes Up). 0x00601670 walks a list of actors (0x00673A50(0) -> +0x68; high list U) (Up).
  0x005F6540(player) at 0x00603B01 (R).
- (c) none direct (R). Possible (U): UpdateWorldData -> collision object vt+0x50 re-syncs a Havok body; 0x00685530 ->
  vt+0x1E8 -> controller; 0x005F6540's line of sight.
- (d) writes no global itself. R [0x00B333C4], [0x00B33E9C] x4, [0x00B36AB8], setting 0x00B36AE0, [0x00B36C10],
  [0x00B36C18], [0x00B1206C], [0x00B148F4], [0x00B3F9A8..B0] (R). CRT rand via 0x0047E060: state per thread
  (`_holdrand`), process-global write only the seed-once byte 0x00B069C3 (Up). 0x0047F750 reads 0x00B3CD7C, 0x0051AC80
  table 0x00B102EC (Up).
- (e) none in body (R). 0x00601670 FormHeap 8 B per extra candidate on picks with two or more qualifying actors, freed
  at the end (Up); 0x006605A0 frees (Up).
- Callees nf: 0x004D7E90, 0x006605A0 (headlines only), 0x005F6540 (own file), 0x0054B010, 0x0054B3E0, 0x0047CAF0;
  partly: 0x00601670, 0x005F7900, 0x00685530; process vslots +0x08/+0x4B4/+0x4BC/+0x4D8/+0x33C/+0x1DC/+0x480/+0x4CC.

### 0x00896000 bhkCharacterController::Update (disputed fn)
- (a) W many `this` fields: flags +0x1F4 bits 3, 5-7, 8, 9, 10, 12, 13, 16; +0x214, +0x218, +0x260 (via callee),
  +0x280, +0x290, +0x2A0 = 0xB (0x00896CDD), +0x2C0, +0x2D8, +0x2DC, +0x2E0, +0x2E8, +0x2F0, +0x300, +0x304, +0x308,
  +0x30C, +0x350, +0x360, manifold copy +0x3BC/+0x3C0/+0x3C4 (R). W the caller's record param +0x00 (dt), +0x0C,
  +0x10 in place (R 0x0089626E-0x00896270, 0x00896265). State object [[this+0x1E8]+8+id*4] vt+0x18(this) (nf).
- (b) none addressed directly (R). The picked body [0x00BA7A5C] may belong to another reference (U).
- (c) Proxy velocity +0x10 written via 0x008AC0B0 (R sites B26/B42/B47; body Up). Body [[this+0x364]+8]: transform
  W via 0x008ABA40 (R site 0x00896626) on every main-path update with a body; then, when its +8 is set, virtual +0xC
  of its +0x14 and 0x008DE950 (Up). Phantom set position 0x008AC080 only in the picked-body branch B48 (R
  0x00896C9D; broad-phase update U). Indirect: 0x00452A10 -> 0x00891560 -> 0x008AC080 (no-collision path, Up);
  0x008902B0 -> 0x008AD7D0; 0x00894E80 (bit-19 path); 0x008AE890.
- (d) **W [0x00B2EFB8]** = 1 or 4 (0x00896BD9), = 4 (0x00896BE3) around the integrate (R). **W [0x00BA7A5C] = 0**
  each substep (0x00896849), R at 0x00896C0C (R). R [0x00BA7A40], [0x00B3F9A8], [0x00BA7A58], [0x00BA7DA0] (R).
  Callee 0x00893B90 writes [0x00BA7A5C]/[0x00BA7A58] (Up; reach U). 0x0088BB60 appends 8 B (heap growth) to the array
  +0x6C/+0x70/+0x74 of [[phantom+8]+0x2B0] (Up). 0x008A7930 -> 0x008A78E0 draws through [0x00BA7DA0] under its own
  critical section (Up). Material property (type 2) of the node from 0x00891160: W +0x40..+0x48, +0x54 incremented
  (R 0x00896F5C-0x00896F6A; gated by flags bit 15 per Up). TLS profiler +0x1A4/+0x1A8 (per thread, R).
- (e) 0x008A6EE0 grows the manifold copy (R call, B52); 0x0088BB60's append (Up).
- Callees nf: vtable +0x58 of this (also via 0x0089F570, pairs around Havok access), 0x00894C70, 0x004D6830,
  0x008B1DD0, 0x008D2C20, 0x00986000, 0x00986300, 0x00891780 (own file), 0x008AE890 (own file), state vt+0x18,
  0x008902B0, 0x008939B0, 0x00894E80, vtable +0x84. Up only: the proxy accessors 0x008AC0A0/0x008AC0B0/0x008AC0C0/
  0x008AC070/0x008AC080/0x008ABDB0, 0x006848D0, 0x008ABA40, 0x0088BB60, 0x008A7930, 0x008A6EE0, 0x00891160.

### 0x00891780 CharacterController::UpdateProbePoints
- (a) R +0x1F4 (bit 0), +0x08, +0x3A0, +0x3A4, +0x248; W +0x380..+0x38F and +0x390..+0x39F only, z twice (R).
- (b) none (R).
- (c) R the 64-byte transform at [[this+0x8]+0x30]+0x70 (R; that it is the phantom's: U). No write, lock, query.
- (d) R [0x00B30AAC], constants 0x00A2FAA0, 0x00A3D360 (R). No write.
- (e) none (R).
- Callees nf: 0x009811E2 (CRT). 0x008AC0C0 and 0x0088FCC0 followed (instructions read in a non-reference copy whose
  bytes of 0x00891780 match; callee bytes in the reference build U).

### 0x008AE890 CharacterProxy::CheckSupport
- (a) proxy: no access (R). Writes only its stack frame (collector, vtable 0x00A55DEC) (R).
- (b) none (R). (c) none (R).
- (d) R [0x00BA9DE4] `_tls_index`, FS:[0x2C], TLS +0x19C, [0x00BA7D9C] when the TLS slot is null (R). No write.
- (e) 0x008A75D0 free of the hit array when it outgrew the inline 8 (R call; that it frees: U), on the TLS allocator
  or the shared [0x00BA7D9C].
- Callees nf: 0x008A75D0 (Up), 0x008AE100 (own file).

### 0x008AE100 CharacterProxy::CheckSupportWithCollector
- (a) proxy: R +0x00, +0x40, +0x64, +0x68, +0x74, +0x78, +0x80, +0x84, +0xA4; no write to the proxy (R). W the out
  record (controller+0x260) +0x00/+0x10/+0x20 (R). Passes the manifold this+0x74 non-const to listeners (R; writes
  U). Hands the collector to 0x008AD070 (R; writes U).
- (b) none in own instructions (R); via vt+0xC, listeners, 0x008AD070: U.
- (c) no world query in own instructions (R). Only candidate: 0x008AD070 (nf). 0x008AE890's file reads in
  0x008AD070's pseudocode a temporary add to the float at [[[proxy+0x30]+8]+0x74]+8 and its restore (Up).
- (d) R [0x00BA9DE4]; TLS +0x19C R, +0x1A4 R/W, +0x1A8 R (per thread). [0x00BA7D9C] when TLS +0x19C is null: its +0x20
  stack top is bumped and reset (R sites). Constants 0x00A372D0, 0x00A37080, 0x00A2F948, 0x00A97C9C.
- (e) three arrays on the Havok stack allocator (bump +0x20, slow path vt+0xC); up to four grows 0x008A6E40 (nf);
  releases via vt+0x10 and 0x008A75D0 (R sites).
- Callees nf: 0x008AD070, 0x008AC3C0, 0x008A6E40, 0x008EC790 (simplex solver, D), 0x008A75D0; `this` vt+0xC,
  listener vt+0x4, allocator vt+0xC/+0x10.

### 0x008AD7D0 CharacterProxy::Integrate
- (a) proxy: R +0x10..+0xA4 (table); W +0x20 (0x008ADFB2, per iteration), +0x10 (0x008AE086), manifold count +0x78
  (0x008ADEE6) and one 0x30 entry (R). Collectors (arg 3/4, owned by 0x008902B0): counts, early-out, sort, shift (R).
- (b) none directly (R). **0x008AC6A0 ("StApplySurf") pushes touched rigid bodies**: motion virtual +0x60 with an
  impulse per manifold entry with collidable type 1 and entity byte +0x92 == 0, listeners +0x10/+0x14 (Up).
  Listeners +0x04/+0x08 at proxy+0x80 (nf).
- (c) R the phantom translation [[phantom+0x1C]+0x30] at entry (R 0x008AD835-0x008AD83F). Each cast = phantom
  vt+0x30 (R 0x008AD913, 0x008ADE4E; = 0x008ECC60 U). Tail 0x008ABAC0 on every path (R 0x008AE08A): W phantom
  +0xA0..+0xAC, AABB + 0x008DE950 when [phantom+0x08] != 0 (Up).
- (d) R [0x00B2EFB8] iteration cap (R 0x008AD884); R [0x00BA9DE4], [0x00A97BCC], [0x00A2FAA8], [0x00A3D65C],
  [0x00A372D0]; [0x00BA7D9C] read and its +0x20 written when TLS +0x19C is null (R sites). Writes no global itself (R).
- (e) two Havok-stack blocks per iteration (vt+0x0C on overflow, target U); grows 0x008A6E40, 0x008A6EE0; frees
  0x008A75D0 (R sites); 0x008A75D0 forwards to [mem+0x10] vt+0x14 above 0x2000 B or with [mem+0x30] == 0 (Up; lock U).
- Callees nf: 0x008EC790, phantom vt+0x30, this vt+0x08 (update manifold) and +0x0C, listener +0x04/+0x08, memory
  vt+0x0C/+0x10, 0x008A6410 and 0x0088FE00 (under 0x008AC6A0), 0x008ABF70, 0x008DE950. Up only: 0x008AF890,
  0x008ABD40, 0x008AC3C0, 0x008A6E40, 0x008AC6A0, 0x008AC640, 0x008A6EE0, 0x008AC530, 0x008A75D0, 0x008ABAC0.

### 0x008ECC60 ShapePhantom::SetPositionAndLinearCast
- (a) phantom: **W +0xA0..+0xAF** (R 0x008ECC82); R +0x08 (world), +0x14 (shape), +0x70 (transform), +0x120/+0x124
  overlap list; +0x28 via 0x008DE950 (Up).
- (b) R each overlapping collidable's +0x00 (shape, type) and +0x1C (filter) (R); overlaps include other characters'
  phantoms (knowledge/threading.md, M). Writes to them only inside 0x008DE950 and the cast functions (U).
- (c) R world+0x74 (collision input +0x00..+0x0C, +0x08, +0x20), world+0x7C (dispatcher +0x190 table, entries) (R).
  0x008DE950 called once (R 0x008ECCE6) with the grown AABB; inside (Up): world+0x88 incremented and decremented
  with plain ADD/SUB, broad phase vt+0x18 on [world+0x64] with phantom+0x28, pair handling 0x008D84F0/0x008D83E0/
  0x008D8370/0x008DE4E0, vfuncs +0x24/+0x28 of [world+8] around it, 0x00899210 when +0x88 drops to 0.
- (d) R 0x00A3D65C (0.5f). Via 0x008DE950: scratch from TLS +0x19C or [0x00BA7D9C] (Up).
- (e) none in body (R); 0x008DE950 stack allocator and 0x008A75D0 (Up).
- Callees nf: shape vt+0x0C/+0x08, the dispatcher's cast functions (-> 0x00935CC0 per triangle, knowledge),
  0x008DE950 and 0x00899210 (pseudocode only). 0x008D9870 read in a non-reference copy (U for the reference).

## 4. Shared state touched from inside an actor's turn

One row per address or structure. R/W per function with its mark; "via" = in a callee. Function short names:
Loop 0x00677EC0, High 0x00605770, Upd 0x00603CA0, Vit 0x005FAAE0, Anim 0x00471C00, Det 0x005F6540, Pkg 0x0063A210,
Head 0x00603500, Ctl 0x00896000 (disputed), Prb 0x00891780, Sup 0x008AE890, SupB 0x008AE100, Int 0x008AD7D0,
Cast 0x008ECC60.

### 4a. Written (or read-modify-written) shared state

| state | written by | read by | note |
|---|---|---|---|
| byte [0x00B3CBD0] | Upd W1 0x0060407E (cond), W0 0x006044AE (R); Anim W1 0x00471DFD (A != 0), W0 0x00471E1C (R) | 0x006CBEE0 interpolator (Up, outside slice) | spans 0x00471C00..ApplyAnimData; an actor that never set it still clears it (Upd C12) |
| dword [0x00B2EFB8] iteration cap | Ctl W 0x00896BD9 (1/4), 0x00896BE3 (4) (R) | Int R 0x008AD884 (R) | image value 10 (data.json) |
| dword [0x00BA7A5C] picked body | Ctl W 0 at 0x00896849 per substep (R); 0x00893B90 (Up, reach U) | Ctl R 0x00896C0C (R) | result channel between a callee and Ctl |
| dword [0x00BA7A58] | 0x00893B90 (Up, reach U) | Ctl (R) | added to the phantom z in B48 |
| dword [0x00B333C0] detection count | Det `ADD [..],1` no LOCK at 0x005F65D6 (R) | Det (same RMW) | Det called from Upd 0x00604F68 and Head 0x00603B01 (R) |
| byte [0x00B3B935] | Loop W 0 at 0x00677ECD, once a call (R) | - | name unknown |
| byte [0x00B3B77D] breath menu | Upd W 0x006047D2, 0x00604845, player only (R) | - | |
| ProcessManager 0x00B3BD00 +0x60 list (shadow step's) | Loop via 0x00674820 add (R call, Up list) | Loop (scan) ; render's shadow step 0x006747C0 (knowledge) | |
| 0x00B3BD00 +0x68 high list, +0x74/+0x78 cursor, +0xA8 count | Loop R/W +0x74/+0x78, W +0xA8 (R) | Loop; Head via 0x00601670 walks a +0x68 list of 0x00673A50(0) (Up; same list U) | an actor leaving the high level unlinks itself (U, Loop C9) |
| 0x00B3BD00 +0xAC budget timer | Loop via 0x00677500 (Up) | | once a call |
| 0x00B3BD00 +0x58 list | High via 0x006748B0 (Up; dead branch, respawn) | | |
| 0x00B3BD00 temp-effect list | Upd via 0x00678D30, splash only (R call) | | |
| [0x00B430FC] singleton (0x007B7650) | Anim via 0x007B7650 lazy create (Up); +0x223C/+0x2240 cache via 0x007AA3C0 (Up) | Upd via 0x007B7650 (+0x2228), 0x007ABD00 (R calls) | |
| frustum cache 0x00B34398, stamp 0x00B069C4, bit 0x00B343FC | Anim via 0x0047F7B0 (Up) | Anim via 0x0047F7B0; 0x00B33EA0 compared (Up) | rebuild inside the loop: U |
| byte 0x00B069C3 CRT seed-once | Head via 0x0047E060 -> srand, once (Up) | | rand state itself per thread (Up) |
| [0x00B3A6A4] singleton | Pkg via 0x00571F90 when null (Up) | Pkg (R use as ECX 0x0063ABC9) | |
| background pathing object [0x00B3BF80] queue | Pkg via 0x006826D0 when [0x00B15800] set (U) | Pkg via 0x006825C0 (U) | |
| light list (shadow scene node) | Upd via 0x007B4280(0) -> 0x007C6FF0 (R call); Anim L+0x1C detach/attach (R calls, owner U) | render (knowledge/threading.md) | |
| sound manager [0x00B33398]+0x24, sounds | Upd via 0x006ADE50/0x0065A970/0x006B7xxx (R calls, U) | | water/drowning only |
| interface (menu 0x415; debug text) | Upd player only (R calls); Pkg via 0x00579B60 debug (R call); Det debug draw 0x0057B8E0 when [0x00B3BD9A] (R call) | | |
| shader accumulator via 0x007ABD00 | Upd (R call 0x00605718; what it writes U) | | fade value of the NPC |
| scene-graph material property (node from 0x00891160) +0x40..+0x48, +0x54 | Ctl (R 0x00896F5C-6A) | renderer: U | flags bit 15 gate (Up) |
| debug display [0x00BA7DA0] | Ctl via 0x008A7930 -> 0x008A78E0 under a critical section (Up) | | |
| Havok world [[phantom+8]+0x2B0] array +0x6C..+0x74 | Ctl via 0x0088BB60 append + growth (Up) | | +8 = world: D (Cast) / U (Ctl) |
| Havok world +0x88 counter (and +0x8C) | Cast via 0x008DE950 (Up); Int via 0x008ABAC0 -> 0x008DE950 (Up); Ctl via 0x008ABA40 -> 0x008DE950 (Up) | same | plain ADD/SUB, no LOCK (Up) |
| Havok broad phase [world+0x64], pair lists | Cast via 0x008DE950 (Up); Int via 0x008ABAC0 (Up); Ctl via 0x008ABA40 (Up) and 0x008AC080 -> 0x008ABAC0 (Up) | casts of all phantoms | |
| world collision input [world+0x74]+0x08 (tolerance) | SupB via 0x008AD070: temporary add and restore (Up, read in 0x008AE890's file) | Cast R [[world+0x74]+8] (R 0x008ECC8C) | the two sides combine to a shared write: D from Up |
| own phantom +0xA0..+0xAC (position) | Cast W (R 0x008ECC82); Int via 0x008ABAC0 (Up); Ctl via 0x008AC080 (Up) | Prb R +0x70..+0xAF block (R, via [[ctl+8]+0x30]); Int R [[phantom+0x1C]+0x30] (R); other actors' casts through overlap lists (U which fields) | own object, visible to other actors |
| controller body [[ctl+0x364]+8] transform +0x70..+0xAC | Ctl via 0x008ABA40 (R site, writes Up) | Ctl via 0x006848D0 (Up) | |
| touched rigid bodies (motion vt+0x60 impulse) | Int via 0x008AC6A0 (Up) | | other references' bodies |
| proxy listeners' targets (+0x80) | SupB vt+0x4, Int vt+0x04/+0x08, 0x008AC6A0 vt+0x10/+0x14 (calls R / Up; writes U) | | |
| Havok fallback allocator [0x00BA7D9C] (+0x20 stack top) | SupB, Int (R sites), Sup via 0x008A75D0 (Up), Cast via 0x008DE950 (Up) - only when TLS +0x19C is null | same | per-thread allocator otherwise |
| 0x008A75D0's shared path [mem+0x10] vt+0x14 | Sup, SupB, Int (call R; path Up) | | blocks > 0x2000 B or [mem+0x30] == 0 |
| FormHeap (0x00401F00 / 0x00401F20) | Loop, Upd, Pkg (R calls); High via 0x005F8000/0x00420050 (Up); Vit via 0x005E7A60 every call (Up); Head via 0x00601670/0x006605A0 (Up); Det via 0x006135F0 (Up); Anim via 0x007016A0 last release (D) | | heap lock 0x00401040/0x00401AA0 per the spec's risks (M there) |
| BaseExtraList GetByType move-to-front, named lock + TLS cache | Loop via 0x00420EF0 (Up); High via 0x0041E210 (R calls; lock from knowledge/render-thread-outside-the-pass.md) | | lock address not in the slice files |
| another actor's process [[tgt+0x58]+4] | Pkg W 1 at 0x0063B0F3 (R) | U | the only proven write into another actor here |
| object from process vt+0x3B0(tgt) +0x04 | Pkg W 3 at 0x0063B3FE (R) | | owner U |
| other listed actors' processes vt+0x17C(0) | Pkg via 0x005E7BE0 (Up; writes U) | | extra-0x23 list |
| other actors' whole updates (extra 0x23) | Loop via vt+0x1C0 (R calls) | | inside actor k's turn |
| the player's list +0x5AC | Head via 0x006605A0 on the player (call R, write Up) | | detection path only |
| the player via process +0x484/+0x1A4 (greeting) | Upd (R calls; writes U) | | |
| the player via 0x0065D270 | Loop via 0x005F2530/0x005F25F0/0x005F2720 (Up) | | when the actor is the player |
| the mount (actor vt+0x380 result) | Pkg mount vt+0x228(frame seconds), 0x005F80D0, 0x005E9A60 (R calls; writes U) | Upd R heading/position/controller +0x32C (R) | |
| the package object (pkg) | Pkg via 0x00566870(pkg, tgt, 1) (U) | Pkg, Head (R) | whether packages are shared forms: U |
| TLS monitor stream +0x1A4/+0x1A8 | Ctl, SupB, Int (R) | same | per thread |
| CRT `_holdrand` | Head via 0x0047E060 (Up) | | per thread (Up) |

### 4b. Read-only shared state (each address as written in the files)

| address | what | read by |
|---|---|---|
| 0x00B333C4 | g_thePlayer | Loop, High, Upd, Vit, Anim, Det, Pkg, Head (R) |
| player fields +0x588 / +0x590 / +0x594 / +0x5D4 / +0x600 / +0x6E5 | | Upd +0x588/+0x5D4/+0x600 (R); Pkg +0x588 (R); Vit +0x6E5/+0x594 (R), +0x590 (Up); Loop +0x590 (Up) |
| 0x00B33E9C | frame seconds | Loop, High, Upd x5, Pkg x6, Head x4 (R); none in Vit, Anim, Det, Ctl, Int (R) or Cast (D) |
| 0x00B332E0 | g_TimeGlobals (via 0x00402BD0/0x00402BA0/0x00402C40/0x00402C00/0x004029D0) | Loop, High, Upd, Pkg (R calls) |
| 0x00B3BCF0 | float of 0x00673B00 | High (Up), passed to Vit (R) |
| 0x00B33B00 (+0x18) and [0x00B33398]+0x10 | thread-dependent flag of 0x0045A500 | Loop, High, Pkg (R calls; Up inside) |
| 0x00B33398 +0x24 | sound manager | Upd (R) |
| 0x00B333CC +0xDC | camera | Anim (R) |
| 0x00B3BB10 / 0x00B3BB0C | camera nodes | Upd (R) |
| 0x00B333A0 | g_TES | High (R) |
| 0x00B333B4 | selected reference | Det (R) |
| 0x00B333B8 | byte | Upd (R); Det via 0x005F3B50 (Up) |
| 0x00B3F9A8..0x00B3F9B0 | vector (zero in the image) | Upd, Head, Ctl (R) |
| 0x00B258DC..E4, 0x00B258E8 | vectors | Upd (R) |
| 0x00BA7A40 | zero vector in the image | Ctl (R) |
| 0x00B35288 | NiRTTI_BSFadeNode | Upd, Anim (R) |
| 0x00B3CD7C | NiMultiTargetTransformController RTTI | Anim via 0x0047CA30 (Up); Head via 0x0047F750 (Up) |
| 0x00B30AAC | /GS cookie | Upd, Anim, Det, Pkg, Ctl, Prb (R) |
| 0x00BA9DE4 | `_tls_index` | Sup, SupB, Int (R) |
| 0x00B14E90, 0x00B14E98, 0x00B1490C, 0x00B1206C, 0x00B148F4 | bytes | Upd 0x00B14E90/0x00B1490C; High 0x00B14E98; Head 0x00B1206C/0x00B148F4 (R) |
| 0x00B3BAA8, 0x00B3BD98, 0x00B3BDA4, 0x00B3C0AB | bytes | Upd 0x00B3BAA8; High 0x00B3BD98/0x00B3BDA4; Pkg 0x00B3C0AB (R) |
| 0x00B3BD9A, 0x00B3BD9C | debug detection flag / filter | Det (R) |
| 0x00B37D80, 0x00B37D88, 0x00B35C1C | dwords | High (R) |
| 0x00B37B38, 0x00B0762C, 0x00B075F0 | | Upd (R) |
| 0x00B15800, 0x00B3BF80 | bUseBackgroundPathing, pathing object | Pkg (R) |
| 0x00B152B0, 0x00B12988 | procedure table, kind names | Pkg (R) |
| 0x00B13980, 0x00B139A4, 0x00B1399C, 0x00B36A80, 0x00B36C18, 0x00B35EB0 | ints / compared value | Pkg (R); 0x00B36C18 also Head (R) |
| 0x00B36AB8, 0x00B36C10 | head-tracking distance, turn threshold | Head (R) |
| 0x00B37A50 | light multiplier | Det (R) |
| 0x00B12DB4, 0x00B02E24, FontManager | debug text | Det (R) |
| settings via 0x00403C00 | 0x00B36CD8 (Loop); 0x00B37D10, 0x00B37D70, 0x00B37B40, 0x00B36AD8 (Upd); 0x00B375F0, 0x00B375F8, 0x00B37600, 0x00B37608, 0x00B37610 (Vit); 0x00B37A58, 0x00B36C88, 0x00B36B40, 0x00B36AA0, 0x00B36A98 (Pkg); 0x00B36AE0 (Head) | R calls; 0x00B37010 (Vit) read direct |
| 0x00B366E0..0x00B36CB8 | GMST-like floats | Det via 0x005463F0 (Up) |
| 0x00B37570, 0x00B37558, 0x00B37550, 0x00B37F38, 0x00B37F30, 0x00BAABE0 | | Vit via callees (Up) |
| 0x00B102EC | table | Head via 0x0051AC80 (Up) |
| world+0x74 collision input, world+0x7C dispatcher | Havok world | Cast (R) |
| other collidables +0x00, +0x1C | overlap list entries | Cast (R) |
| constants (0x00A2.., 0x00A3.., 0x00A4.., 0x00A6.., 0x00A9.., 0x00B2E780, 0x00B2E784) | read-only data | per function file |

## 5. What this says for threads (derived)

Derived from sections 3-4. "Allows" = facts in the map that keep a part to its own actor; "blocks" = a shared write
with its address and writer; "unknown" = a callee on the path not followed. No recommendation beyond this.

### The character controller's casts: support check 0x008AE890/0x008AE100, integrate 0x008AD7D0, cast 0x008ECC60
- Allows: 0x008AE890 writes only its stack (R); 0x008AE100 writes no proxy field, only the out record controller+0x260,
  its TLS stream and its allocator (R); 0x00891780 writes only controller +0x380..+0x39F (R); all scratch goes through
  the thread's allocator TLS +0x19C when it is set (R sites).
- Blocks (integrate / cast):
  - [0x00B2EFB8]: written by 0x00896000 around every integrate (R 0x00896BD9, 0x00896BE3), read by 0x008AD7D0 (R
    0x008AD884) - two integrates at once read each other's cap.
  - [0x00BA7A5C]: cleared by 0x00896000 per substep (R 0x00896849), written by 0x00893B90 (Up) - shared result channel.
  - Havok world +0x88 counter and broad phase: 0x008DE950, called by 0x008ECC60 (R 0x008ECCE6), 0x008ABAC0 (Int tail,
    Up) and 0x008ABA40 (Ctl, Up); the plain ADD/SUB and the broad-phase/pair writes are Up.
  - The phantom position +0xA0 written per cast (R 0x008ECC82) while other actors' casts walk overlap lists holding
    that phantom (knowledge M) - an order dependence at least (D); which fields the cast functions read: U.
  - Touched bodies pushed by 0x008AC6A0 (Up).
  - [[phantom+8]+0x2B0] array append by 0x0088BB60 from 0x00896000 (Up).
- Blocks (support check): the temporary write into [world+0x74]+0x08 inside 0x008AD070 (Up, from 0x008AE890's file),
  read by every cast (0x008ECC60 R 0x008ECC8C) - D, resting on pseudocode.
- Unknown: 0x008AD070 (the support check's world query), proxy vt+0x08/+0x0C, listeners at proxy+0x80
  (+0x04/+0x08/+0x10/+0x14), the dispatcher's cast functions, 0x008EC790, controller vt+0x58 (called in pairs around
  Havok access - lock or getter, U), the state update vt+0x18; whether workers have TLS +0x19C (else the shared
  [0x00BA7D9C] +0x20 is bumped without a lock, R sites). Who calls 0x00896000 is unknown (no static caller), so where a
  pre-pass would take it from is not in the map. 0x00896000 itself is disputed (a debug-colour label only).

### Detection 0x005F6540
- Allows: the target is only read (R, no store through EBP); no Havok in the body (R); no clock (R).
- Blocks: [0x00B333C0] RMW without LOCK (R 0x005F65D6). Reads of the target's (the player's) process state and AVs
  that the player's own update may write in the same phase (D, order).
- Unknown: 0x005F2820 line of sight (nf; its pseudocode references 0x00BA7D9C); process vt+0xA8 (the store) and
  vt+0x3B0; X = vt+0x330 object, its +0x180 and the list 0x006135F0 prunes and frees (Up) - whose object: U;
  0x005463F0 purity (U).

### Package update 0x0063A210
- Allows: its own instructions write no global (R claim 2); most writes are this process's fields (R).
- Blocks: [[tgt+0x58]+4] = 1 into another actor's process (R 0x0063B0F3); [EAX+4] = 3 into vt+0x3B0(tgt)'s object
  (R 0x0063B3FE, owner U); the pathing queue on [0x00B3BF80] via 0x006826D0 (U); [0x00B3A6A4] lazy write via
  0x00571F90 (Up); FormHeap frees on the done path (R); the interface debug text 0x00579B60 (R call, gated by the
  selected reference).
- Unknown: ~60 process vtable slots (the procedure handlers +0x508..+0x58C, +0x18 re-evaluation, +0x188, +0x194,
  +0x3C0), the procedure functions 0x006384B0, 0x0062A0E0, 0x00654F10, 0x00628330, 0x0062B5C0, 0x00633DF0,
  0x00630100, 0x005E7BE0 (other actors' process vt+0x17C), 0x00566870, the movement callees 0x00603160/0x00603320/
  0x00685530, 0x00602050/0x005F0410 (-> 0x0088D0E0), kAttackUpdate 0x005FCAB0, the mount's vt+0x228. Havok reach U.

### Head tracking 0x00603500
- Allows: its own writes are this actor's +0xDC/+0xE0/+0x100, its head-tracking object, its process's look target, and
  its own skeleton nodes found under actor+0x3C (R/D); CRT rand state per thread (Up) - a determinism point for a
  class B check, not a race.
- Blocks: detection 0x005F6540 called at 0x00603B01 (R) with its [0x00B333C0] RMW (R); 0x006605A0 writes the player's
  list +0x5AC and frees (call R 0x00603AD6, write Up); the seed-once byte 0x00B069C3 (Up, written once).
- Unknown: UpdateWorldData x3 (a collision object's vt+0x50 may re-sync a Havok body, U); the turn 0x00685530 ->
  vt+0x1E8 (controller reach U); 0x00601670 (walks an actor list, allocates; Up); 0x0054B010/0x0054B3E0/0x0047CAF0
  (the solve and twist, nf); process vslots +0x4B4/+0x4BC/+0x4D8/+0x480. Reads the look target's and the player's
  positions (order, D).

### Vitals 0x005FAAE0
- Allows: its own instructions write only this+0xBC (R) and its own AVs through calls (R sites); no global written
  (R); no Havok, no allocator in its 14 direct targets (R); its time is the argument, not a global (R).
- Blocks: none read in its own instructions. FormHeap through 0x005E7A60 every call (Up) - the heap's lock is shared.
  Reads player +0x6E5/+0x594 (R) that the player's update may write in the same phase (D).
- Unknown: actor vslots +0x2E4, +0x33C, +0x2D8, +0x198, +0x330 (the 2.2 ms may sit there), MagicCaster +0x30, process
  +0x2C0/+0x2D0/+0x468, 0x005E07D0, 0x00420E90 -> 0x0042AFF0, 0x005F2720/0x005F2530 (Up).

### The anim step 0x00471C00
- Allows: its body touches no Havok (R); its own writes are the anim data's dirty word, the root's world bound (hidden
  path) and actor+0xC4 (R).
- Blocks: [0x00B3CBD0] written 1/0 (R 0x00471DFD, 0x00471E1C), also by 0x00603CA0 (R), read by the interpolator
  0x006CBEE0 (Up). The frustum cache 0x00B34398/0x00B069C4/0x00B343FC (0x0047F7B0, Up) and the singleton cache
  0x00B430FC +0x223C/+0x2240 (0x007AA3C0, Up), lazy creation of 0x00B430FC (Up).
- Unknown: 0x0047C740 (the controllers' evaluation, likely the bulk; nf), the owner L+0x1C detach/attach (shared light
  list or not, U), actor vcalls +0x27C/+0x380/+0x388/+0x168/+0x334.

## 6. Open

### Callees not followed on a candidate's path
- Controller: 0x008AD070, 0x008EC790, 0x008AC3C0 (Up only), 0x008A6E40, 0x008A75D0, 0x008AC6A0 (Up) with 0x008A6410
  and 0x0088FE00, 0x008ABAC0 (Up), 0x008DE950 and 0x00899210 (Up), 0x008ABF70, proxy vt+0x08/+0x0C, listener
  vt+0x04/+0x08/+0x10/+0x14, phantom vt+0x30 (= 0x008ECC60 U), shape vt+0x08/+0x0C, the dispatcher's cast functions
  (-> 0x00935CC0), memory vt+0x0C/+0x10, controller vt+0x58 / 0x0089F570, state vt+0x18, 0x008902B0 (knowledge
  only), 0x008939B0, 0x00894C70, 0x00894E80, 0x00893B90, 0x0088BB60, 0x008ABA40, 0x006848D0, 0x008AC080,
  0x00891560, 0x00891160, vt+0x84.
- Detection: 0x005F2820, 0x005463F0, 0x005F3B50, 0x006135F0, 0x00612810, process vt+0xA8/+0x3B0/+0x3AC/+0x35C/
  +0x36C, actor vt+0x330, 0x004D7E90.
- Package: the procedure handlers (process +0x508..+0x58C), 0x006384B0, 0x0062A0E0, 0x00654F10, 0x00628330,
  0x0062B5C0, 0x00633DF0, 0x00630100, process +0x18/+0x188/+0x194/+0x36C/+0x3C0/+0x20, 0x005E7BE0, 0x00566870,
  0x00566DC0, 0x00603160, 0x00603320, 0x00685530, 0x00602050, 0x005F0410, 0x005EAE70, 0x005E9A60, 0x005F80D0,
  0x006825C0, 0x006826D0, 0x0063A020, 0x00635E20, 0x00693210, 0x005FCAB0; slot 3 0x0063BA30 (unread).
- Head tracking: 0x006605A0, 0x00601670, 0x0054B010, 0x0054B3E0, 0x0047CAF0, 0x00685530, 0x005F7900, process
  +0x4B4/+0x4BC/+0x4D8/+0x480/+0x4CC, NiAVObject vt+0x74 -> collision object vt+0x50.
- Vitals: actor vt+0x2E4/+0x33C/+0x2D8/+0x198/+0x330, MagicCaster +0x30, process +0x2C0/+0x2D0/+0x468, 0x005E07D0,
  0x005E7A60, 0x005F2720, 0x005F2530, 0x00420E90 -> 0x0042AFF0, 0x0065D270.
- Anim step: 0x0047C740, owner vt+0x88/+0x84, 0x0047F7B0 (partly), 0x007AA3C0 (Up), actor vt+0x27C/+0x380/+0x388/
  +0x168/+0x334.
- Around them: 0x00605770's vt+0x1C0 slot, vt+0x368 slot and process slot 4 resolution (vtable bytes at 0x00A6FE5C
  and the Creature/PlayerCharacter equivalents, HighProcess vtable 0x00A71814 not exported); 0x005F2530/0x005F25F0/
  0x005F2720 from the loop; 0x00677500; 0x00674820.

### Manifest questions the maps did not answer
- 0x00677EC0: which callees make the 4.1 ms outside the high update - not timed (file: unverified). Whether an
  actor's turn reads anything an earlier actor wrote - partly (section 2); the vfuncs +0x1D4/+0x1D8/+0x1DC and the
  process branch are nf.
- 0x00605770: what each later step reads from an earlier one - not visible at its level (file: U, goes through the
  actor and process objects inside callees).
- 0x00603CA0: where it reaches 0x00896000 - not directly (R); through one of its 103 virtual calls: U. Its large
  callees by structure: listed, costs U.
- 0x005FAAE0: purpose answered as proposal; the split of its 2.2 ms - not measured.
- 0x00471C00: where it leaves its result for ApplyAnimData - the controllers' writes (0x0047C740 nf): U.
- 0x005F6540: who calls it - ten callers listed; what the other eight pass: U.
- 0x0063A210: "on which other objects" - partly; the procedure handlers nf.
- 0x00603500: whether it touches anything but the actor's own animation data - yes (the player's list via 0x006605A0,
  Up; detection); the Havok side of UpdateWorldData: U.
- 0x00896000: every global it writes - [0x00B2EFB8], [0x00BA7A5C] (R); callees' writes U. Whether it moves the
  phantom and when - only in the picked-body branch B48 directly (R); per cast and at the integrate's end through
  callees (Up / U). Who calls it and fills `param` - U.
- 0x008AE890: whether it casts - not itself (R); 0x008AD070 nf.
- 0x008AE100: the world queries it makes - only via 0x008AD070, nf.
- 0x008AD7D0: whether it writes the broad phase and pushes what it touches - Up only (0x008ABAC0, 0x008AC6A0).
- 0x008ECC60: whether a cast only reads the world - no: it writes the phantom (R) and, through 0x008DE950, world+0x88
  and the broad phase (Up); whether that counter is a lock: U.

## 7. Naming decisions for the architect (listed, not resolved)

1. 0x00603500: seed "Actor animation step", the spec's "the animation step 0x00603500" and 0x00603CA0.md's purpose
   step 8 ("the animation step 0x00603500(frameSeconds)") against 0x00603500.md's `Actor::UpdateHeadTracking`
   (derived there: no skeleton animation, head tracking). knowledge/threading.md calls it "AnimStep" with the spine
   twist's bones.
2. Actor vtable slot +0x1C0: "Unk_70" (knowledge/render-thread-outside-the-pass.md) against `Actor::UpdateHigh`
   (0x00605770.md); slot resolution itself U.
3. 0x00677EC0 `ProcessManager::UpdateHighActors` against 0x00673C10's "UpdateHighProcesses (proposal)"
   (knowledge/render-thread-outside-the-pass.md); knowledge/threading.md's table also says "UpdateHighProcesses" for
   the high loop's own work.
4. 0x009828C0: `_ftol` in 0x005F6540.md (C9) and 0x00896000.md, against "a random draw / generator" in
   knowledge/render-thread-outside-the-pass.md (lines 222, 371) and docs/performance-backlog.md (line 599).
5. ActorAnimData +0x94: knowledge/render-thread-outside-the-pass.md:711 (ApplyAnimData calls NiAVObject::Update "on
   the node at +0x94") against 0x00471C00.md (`FLD float [EBX+0x94]` at 0x00471E07, a float time).
6. actor+0xB0: "life state" (0x00605770.md, meaning U; 0x0063A210.md, proposal), "process level?" (0x00471C00.md),
   "state" (0x00603CA0.md, 0x00603500.md), "unknown state, compared with 3" (0x00677EC0.md).
7. [0x00B3BCF0]: "the process manager's time stamp" (0x00605770.md, proposal) against "frameSeconds" for 0x00673B10's
   argument in knowledge/render-thread-outside-the-pass.md (raised in 0x005FAAE0.md: the stamp idiom needs a running
   clock).
8. Spine twist start: knowledge/actor-head-tracking.md "starts at Spine2" against 0x00603500.md claim 8 (ECX = Spine2's
   parent); the knowledge file's "hold 4-10 s" for +0xDC against 0x00603500.md claim 4 (never a gate in this
   function).
9. The controller object: 0x00603CA0.md's 0x0065A2C0 "controller-like object" (+0x1F4 flags bit 8, +0x2A0 "requested
   state := 0 or 5", +0x2E8 "vertical speed", +0x32C "pitch", +0x338) and 0x00896000.md's `this` (+0x1F4 flags,
   +0x2A0 written 0xB each substep, +0x2E8 `velocity.z`, +0x32C angle); same class U. Class name: 0x00896000.md
   `bhkCharacterController` (U), 0x00891780.md `CharacterController`; knowledge/character-controller-contacts.md and
   threading.md treat 0x00896000 as the controller's update without a class.
10. Proxy class: 0x008AE890.md/0x008AE100.md/0x008AD7D0.md `CharacterProxy::*` (Havok `hkpCharacterProxy` /
    `hkCharacterProxy` U), knowledge/character-controller-contacts.md `bhkCharacterProxy` (vtable 0x00A9667C, RTTI)
    for 0x008902B0's object.
11. The phantom's transform: 0x008AD7D0.md reads the start position at [[phantom+0x1C]+0x30]; 0x008ECC60.md and
    0x00891780.md use the inline transform phantom+0x70 with the translation at +0xA0. Whether [phantom+0x1C] points
    at phantom+0x70: U.
12. 0x007B7650: "BSShaderAccumulator singleton per re/ledger.md" (0x00603CA0.md, field +0x2228) against "singleton
    getter 0x00B430FC, lazily created" with a form-id list at +0x2230 (0x00471C00.md).
13. 0x005F2530 / 0x005F2720: "health regen" / "fatigue regen" (0x005FAAE0.md, Up) against "three per-actor steps with
    2.0 ... call 0x0065D270 for the player" with 0x005F25F0 (0x00677EC0.md).
14. The manifest against the files: 0x00605770 "recursive (calls itself twice)" - the file reads no self call (R);
    0x008AE100 "6 callees" - meta.json lists five; 0x0063A210 0.9 ms (manifest) against 0.8 ms (spec) - the file
    marks the cost unverified.
15. 0x00605770.md places "movement and the character controller" inside Actor::Update (U, per manifest and backlog);
    0x00603CA0.md C5 reads that 0x00603CA0 does not call 0x00896000 directly.
16. Frame-time reads in 0x00603CA0: five (0x00603CA0.md C3, R) against "seven places" in docs/performance-backlog.md
    and plugin/src/ActorFarUpdate.cpp; "only tested against 900" in the backlog misses the second use (C2).
17. For the knowledge owners, not a conflict: 0x00896000.md claim 11 answers knowledge/threading.md's "whether the
    controller writes a node" - it writes a material property of the node from 0x00891160 (R), gated by flags bit 15
    (Up); whether the renderer sees it: U. 0x005F6540.md C9 asks render-thread-outside-the-pass.md's owner to re-read
    "draw against 50 and 10" as an ftol (item 4).
18. 0x00896000 is disputed on B54 (section header); its name, the class of `this` and its caller stay open with it.

## Added by the architect after the synthesis (2026-11-14)

- Spot checks of the synthesis against the instructions (read, re/export/actor-high-update/*/asm.s): 0x005F65D6
  `ADD dword ptr [0x00B333C0],1`, 0x0063B0F3 `MOV dword ptr [ECX+4],1`, 0x00896849 `MOV dword ptr [0x00BA7A5C],0`,
  0x008ECCE6 `CALL 0x008DE950` - all four as the maps say.
- **Where the controller's update 0x00896000 is called from**: no direct call anywhere; three data references
  (0x00A6FAE4, 0x00A752C4, 0x00A9689C - vtable slots; Ghidra's references, read). In the running game (measured:
  OBVR's render sampler in the gate horde, run obng-profile-gate-guards/20261114-151935, 63 samples inside
  0x00896000 with a return address outside it) the nearest caller is **0x005E0660** (39 samples, return 0x005E06A6),
  then 0x005232D0 (11) and 0x00643340 (6); the chains above read 0x005E0660 < 0x00643340 < 0x00628790 < 0x005F1910
  and 0x005E0660 < 0x006814B0 < 0x00643340 < 0x00628790 (the deeper words can be stale). So the controller runs
  inside an actor's movement, under the high process's 0x00643340 - none of these four is mapped. They are the next
  slice: without them it is not known what the movement does with the controller's result in the same turn.

## The second slice, actor-movement (2026-11-14): one correction and the cast's world write

- **Withdrawn: "the controller's update is called from 0x005E0660 under 0x00643340"** (the architect's note above,
  from the sampler's stack words). The slice mapped those functions and all three refuters held it: 0x005E0660 is
  a height getter (three virtual calls, (A.z - B.z) * scale, no call that can be 0x00896000 directly), 0x00643340 an
  actor-value sum, 0x005F1910 and 0x005232D0 base actor-value getters, 0x00628790 a point along a line, 0x006814B0
  a ray cast "is the reference ahead". The words in the samples were stale stack contents - the first word too,
  which knowledge/render-thread-outside-the-pass.md had only said of the deeper ones. **Who calls 0x00896000 is
  still not known**: three vtable slots (0x00A6FAE4, 0x00A752C4, 0x00A9689C) hold it; an entry probe that records
  the return address at [ESP] settles it.
- **Every cast moves the phantom in the world's broad phase** (re/functions/0x008DE950.md, proposed
  hkPhantom::updateBroadPhase; read): with the world unlocked it increments `[world+0x88]` (load, INC, store at
  0x008DE995-0x008DE9A2, no LOCK), calls the broad phase `[world+0x64]` vfunc +0x18 with the phantom's handle and
  the new AABB, removes and adds the overlap pairs through the dispatcher `[world+0x68]`, decrements the counter
  (0x008DEB15) and drains pending operations when it reaches 0. With the world locked
  (`[world+0x88] + [world+0x8C] != 0`) it queues a record of type 0x11 through 0x00898820 and returns. So two casts
  of different phantoms cannot overlap in time as the code stands: the counter, the broad phase and the pair tables
  are shared and unguarded (the race is derived; what the broad phase writes inside is not followed).
- **The support check's tolerance** (re/functions/0x008AD070.md, read): it adds to the float at `[world+0x74]+0x08`
  (0x008AD0A1-0x008AD0AC), calls the phantom's vtable +0x34 once and restores the saved value (0x008AD0BB); no lock.
  Two proxies interleaved would leave a widened tolerance behind (derived).
- 0x008ABA40 (proposed hkShapePhantom::setTransform) and 0x008ABAC0 both end in the broad-phase update.
