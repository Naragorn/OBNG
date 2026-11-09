# The render thread outside the scene pass: where the frame's other 70 % go

Status: knowledge, written 2026-11-08 by the architect from OBVR's render
sampler (run `artifacts/shadow/obng-profile-gold-road-single/20261108-133456`,
the single pass with the carry, road spot 2 of the Gold Road scenario, the
machine otherwise idle) and from Ghidra's decompilation of the hot functions
(export `re/export/frame-update/`, slice `re/slices/frame-update/`). The
names below are the architect's reading of the decompilates, **not refuted
by the slice workflow**; each is marked. Nothing of this is built.

Marks: **read** = read in the binary (address given), **measured** = from
the sampler, **derived** = follows from the two, **unverified** = not
checked.

## How to read the sampler here

OBVR's render sampler (`../OBVR/src/render/RenderSampler.h`) suspends the
render thread about every millisecond and records the eip plus up to eight
code addresses found by **scanning the stack upward** (StackScan.h, "the
hang watchdog's way"), not by walking frame pointers. The eip is exact.
The first stack word (w0) is the return address of the running function in
most cases. Deeper words are often **stale**: a function with a large frame
leaves old return addresses in it, and a scan cannot tell them from live
ones. Measured consequence in this run: 9.8 % of all out-of-pass samples
are vrclient.dll (the pose wait, idle) with the Havok functions 0x008ABF70,
0x008AC640, 0x00891850 and 0x00893B90 "on the stack" - the previous step's
frames under OBVR's wait, not callers. `perf-functions.py`'s inclusive
column therefore overstates big-frame functions; everything below rests on
exclusive samples and, where a caller is named, on w0 right after a CALL.

Samples at road spot 2 (marks s2-y0 to s2-y45): 14 479 in all, 10 227
outside the scene pass (70.6 %), 4 252 inside. The frame is 13.4 ms at the
75 Hz vsync in every run (measured). "ms a frame" below is the sample share
times 0.706 times 13.4 ms (derived).

## The out-of-pass samples by area (measured)

| Area | Share | ms a frame (derived) |
|---|---|---|
| vrclient.dll (WaitGetPoses, idle) | 31.6 % | 3.0 |
| game code 0x0040xxxx-0x0068xxxx (TES, cells, actors, menus, scripts) | 23.7 % | 2.2 |
| NiMain 0x0069xxxx-0x0074xxxx (the scene graph update) | 14.6 % | 1.4 |
| Havok 0x0089xxxx-0x0092xxxx plus its GSK code at 0x0093xxxx (below) | 10.7 % | 1.0 |
| CRT 0x0098xxxx-0x00A2xxxx (mostly the square root's slow path) | about 3 % | 0.3 |
| ntdll.dll | 6.9 % | 0.65 |
| OBVR.dll | 3.9 % | 0.4 |
| OblivionWalkers.dll | 0.7 % | 0.07 |
| renderer 0x0075xxxx-0x0088xxxx | 1.0 % | 0.1 |

Of the ntdll share, 4.4 % of all out-of-pass samples sit directly under
0x007AC9A0 (the shader accumulator's per-pass list draw,
re/functions/0x007AC9A0.md) called from the NiNode walk 0x0070AB40: render
passes that run **outside OBVR's scene-pass bracket** (which ones is
unverified; the water's reflection or the actor shadow maps are the
candidates) wait in a lock inside D3D9/DXVK for about 0.4 ms a frame
(derived).

## The hot functions, named by reading (exclusive share of the out-of-pass samples)

### The scene graph update: Gamebryo's NiAVObject::Update tree, about 17 %, 1.6 ms a frame

All read in `re/export/frame-update/<address>/pseudo.c` and `asm.s` on
2026-11-08; the Gamebryo method names are proposals that match the code's
shape and OBVR's two seeds for 0x00707370 (`kUpdateSelectedDownwardPass`,
`kUpdateNodeTransforms`, GameAddresses.h:364/1259).

| Address | Proposed name | Share | What the code does (read) |
|---|---|---|---|
| 0x0047C930 | NiAVObject::UpdateObjectControllers(float time, bool properties) | 4.3 % | When the bool is set, walks the property list at this+0x9C (NiTListBase nodes: next +0, item +8) and calls vfunc +0x50(time) on every property whose +0x0C (its controller) is non-null; then walks this+0x0C (NiObjectNET controllers, next at +0x34) calling vfunc +0x54(time). Hot at 0x0047C941/0x0047C948/0x0047C955: the list walk itself, for every object in the loaded scene every frame, most lists empty. Callers: 0x0070A0D0 (34 %), 0x00707580 (32 %), 0x007075B0 (27 %) (measured, w0). |
| 0x0070A0D0 | NiNode::UpdateDownwardPass(float time, bool controllers) | 2.8 % (recursive) | UpdateObjectControllers(time, 1) when the bool is set; vfunc +0x74 (UpdateWorldData); world bound radius this+0x2C := 0; for each child in the NiTArray at +0xAC (data +0xB0, count ushort +0xB6) vfunc +0x60(time, bool) and then the child's bound merged into this+0x20..+0x2C (copy when own radius is 0, else NiBound::Merge 0x0072A6B0). Entered from 0x00707370; 92 % of its callers are itself (measured). |
| 0x0070A190 | NiNode::UpdateSelectedDownwardPass(float time) | 1.1 % | UpdateObjectControllers(time, flags bit 3); UpdateWorldData when flags bit 2; per child with flags bit 1: vfunc +0x68 when bit 4 (the rigid variant) else +0x64; the same bound merge. Flags = the ushort at this+0x18. |
| 0x00707370 | NiAVObject::Update(float time, bool controllers) | - | vfunc +0x60(time, bool), then parent (+0x1C) vfunc +0x94 (UpdateUpwardPass). thiscall, RET 8 (knowledge/skinned-part-attach.md agrees). 146 callers. |
| 0x007073A0 | NiAVObject::UpdateSelected(float time) | - | vfunc +0x64(time), then the parent's +0x94. |
| 0x00707580 | NiAVObject::UpdateDownwardPass (the leaf object's) | - | UpdateObjectControllers(time, 1) when the bool is set; vfunc +0x74 (UpdateWorldData); vfunc +0x78 (UpdateWorldBound). |
| 0x007075B0 | NiAVObject::UpdateSelectedDownwardPass | - | the same with the flag bits 3 and 2 as gates. |
| 0x0070C120 | NiAVObject::UpdateWorldData | 1.3 % | world (+0x64, 13 dwords) := parent ? parent.world * local (+0x30) through 0x0053D7A0 : local; then the collision object at +0xA8, vfunc +0x50. |
| 0x0053D7A0 | NiTransform::operator*(const NiTransform&) | 2.3 % | rotation through 0x007100A0, translation through 0x007101F0 plus the scaled add, scale product (asm 0x0053D7C0-0x0053D800). knowledge/bone-palettes.md knows it as the bone compose: the same function. |
| 0x007100A0 | NiMatrix3::operator*(const NiMatrix3&) | 1.0 % | nine dot products. |
| 0x007101F0 | NiMatrix3::operator*(const NiPoint3&) | 1.1 % | three dot products. |
| 0x0072A6B0 | NiBound::Merge(const NiBound*) | 1.7 % | sphere merge: distance of the centres through the CRT square root 0x00982C30 (calls at 0x0072A74F and 0x0072A7F7), the containment cases, the new centre and radius. |
| 0x0072A820 | NiBound::Update(NiBound& out, const NiTransform&) | 0.5 % | centre := translate + scale * (rotate * centre), radius *= scale. |
| 0x00722AA0 | NiGeometry::UpdateWorldBound | - | model bound from the data (+0xB4)+0x0C, or the skin bound through 0x0072BB30 when a skin instance (+0xB8) exists; NiBound::Update with the world transform; the flag bit 7 at +0x18 kept by 0x0072A0A0's answer. |
| 0x0072BB30 | NiSkinInstance world bound (proposal) | 0.6 % | per bone NiBound::Update with the bone's world transform and NiBound::Merge; 0x00718A80 and one NiTransform compose at the end. |
| 0x0070A360 | NiNode::UpdateWorldBound | 0.3 % | the children's bounds merged. |
| 0x00709EA0 | NiNode deleting destructor (scalar) | - | 0x00731AE0 then FormHeap_Free when bit 0. Stale-frame noise in the chains, not update work. |
| 0x0088EDC0 | bhkNiCollisionObject::UpdateWorldData (proposal) | 0.8 % with its leaf | the collision object's vfunc +0x50 that 0x0070C120 calls: gated on the global byte 0x00BA7A8C (3 = none of it; 2 = no re-sync) and the flags at +0x0C (bit 6: 0x0089EAE0 once, bit 0: when 0x00607840 on +0x10 says the body is in a world, or +0x14 < 1.0, or +0x1C == 1, vfunc +0x64). |
| 0x00607840 | bhkRigidBody in a world (predicate; proposal) | 0.8 % | returns p && [p+8] && [[p+8]+8] != 0. Called only from 0x0088EDC0 (measured, w0 100 %). |

Who drives the tree each frame (read in the callers' decompilates):

- 0x004823D0 **GridCellArray::Update(float time)** (xOBSE: +0x0C size,
  +0x10 grid of 8-byte entries; deepwiki llde/xOBSE 2026-11-08): calls
  0x004CCCE0 (the Havok world's vfunc +0x80 on g_bhkWorldM, read), then for
  every grid cell: the four border rows on alternating frames (the byte
  0x00B3440C toggles each call and selects the checkerboard half), the
  interior every frame, 0x004D4970 on the cell. 9.9 % inclusive (measured,
  stale-prone).
- 0x004D4970 **TESObjectCELL::UpdateSelectedNodes(float time)** (proposal):
  with cellProcessLevel (+0x26) == 6 and flags0 (+0x24) bit 0, the object
  from 0x00424180's vfunc +0x80; then children 2..5 of the cell's NiNode
  (+0x54), each one's child 3, when its flags bit 0 (APP_CULLED) is clear:
  NiAVObject::UpdateSelected(time). The cell's node has at least four fixed
  sub-nodes (derived).
- 0x00603CA0 (the actor update, knowledge/actor-head-tracking.md) through
  0x00471F20 (`kApplyAnimData`, OBVR) calls NiAVObject::Update
  0x00707370 on the actor's node (measured chain 0x0047C930 <- 0x00707580
  <- 0x0070A0D0 <- 0x0070A0D0 <- 0x00707370 <- 0x00471F20 <- 0x00603CA0,
  1.1 % of the samples under it).

### Havok's step: about 10.7 %, 1.0 ms a frame, its cause unresolved

Function identities from Havok's own monitor-stream tags written next to
`rdtsc` (read in the decompilates; "Tt"/"Lt"/"St" prefixes are Havok's
timer begin/list/split markers) and from the code's shape; the class names
are Havok 4.x knowledge, **unverified** against a Havok SDK:

| Address | Tag / shape | Proposed identity |
|---|---|---|
| 0x008E4590 | per entry: scaled, clamped integer AABB per axis, 0x008E3C30/0x008E3E90/0x008E4210 per axis | hk3AxisSweep::updateAabbs (the broadphase); the same address knowledge/cell-change-hitches.md saw waiting on a lock at cell borders |
| 0x008DE950 | checks the world's lock counters (+0x88/+0x8C, error 0x11), allocates two stack arrays from the thread's hkMemory stack (TLS +0x19C) sized by [world+8]+0x2A8 | hkWorld's broadphase/entity update step (proposal) |
| 0x00893B90 | 3437 bytes, frame of 0x1C4+, recursive-looking, calls 0x00891850, 0x00891CC0, 0x008A7930 | the simulation's collide step (proposal); its big frame is the source of the stale Havok words under the pose wait |
| 0x00908DE0 | "LtBvTree", "QueryTree", "StNarrowPhase" | hkBvTreeAgent::processCollision (the tree query, then the narrowphase per child) |
| 0x00906090 | "TtMopp" | hkMoppBvTreeShape query (the terrain and statics) |
| 0x00902590 | "LtCvsListAgent", "checkHull", "Stchild" | hkConvexListAgent::processCollision |
| 0x0093F250 | "TtGsk", "StGsk" | hkGskConvexConvexAgent / GSK closest-points agent; calls 0x0093C690 |
| 0x0093C690 | 795 lines, feature case tables at 0x00A99F04/0x0C/0x0E, calls 0x0093B000, 0x0093B220, 0x0093C0C0, 0x0093C350, 0x008D1A30 | hkGsk::getClosestFeature (the GJK-style convex feature walk), 2.0 % exclusive |
| 0x00916C00 | 4-wide SIMD max-dot over vertex blocks (+0x30 data, +0x34 blocks), returns the vertex and its id | hkConvexVerticesShape::getSupportingVertex, 1.2 % |
| 0x008D1A30 | closest points of two segments with clamping flags | hkLineSegmentUtil closest points, 1.0 % |
| 0x008A78E0 | for each listener in +0x08..+0x0C: vfunc +0x1C; LeaveCriticalSection(+0x14) | collision callback dispatch under Bethesda's critical section |

Note: the 0x0093xxxx functions are Havok, not CRT; perf-functions.py's area
split by address is wrong for them. The CRT begins at about 0x00980000
(the square root 0x00982C30, __87except 0x0099E2B9, setSBUpLow 0x0098F69B).

Attribution: 65 % of the Havok samples have **no engine frame** among the
eight scanned words and only 23 % show one of the step functions (measured),
so the scan cannot say whether this is the engine's own step (the player's
character proxy against the terrain's MOPP, the loaded cells' bodies) or
collision work that a plugin's bodies cause each step - OBVR's hand bodies
(`../OBVR/src/game/HandBodies.cpp`) and OblivionWalkers' colliders are in the
world in this run. **Unresolved.** The cheapest decider is a run of the same
scenario with OBVR's hand bodies off, if OBVR has that switch: it does - OBVR 2026-11-08: `[Hands] BodyCollision=0` takes the
keyframed hand and weapon bodies out of the Havok world (WorldPush.cpp,
`bodies.enabled = inPlace && config.hands.bodyCollision`), `PushWorld=0`
also stops the ray pushes, and `[Hands] PhysicsRate` (0 = the engine's own
step rate; above 0 or -1 raises the step count for **all** physics) must be
the same in both runs. The profile runs set none of these keys (read in
their OBVR-test.ini), so the deployed OBVR.ini's values held - 1, 1 and 0 in
OBVR's repository copy (read), unverified for the deployed file. The
comparison is prepared: `tools/hand-scripts/obng-profile-gold-road-nobodies.txt`
is the single-pass profile scenario with `ini Hands BodyCollision=0`, the
original now states `BodyCollision=1` and `PhysicsRate=0` explicitly.
**Run 2026-11-08 17:56: the hand bodies are acquitted** - see "Runs of
2026-11-08" at the end of this file.

### Per-reference animation gate: 2.7 %, 0.26 ms a frame, mostly cache misses

- 0x004E3F30 **TESObjectREFR::UpdateAnimation (vtable +0x160, xOBSE Unk_58;
  proposal)**: a function Ghidra had left undefined behind 0x004E3490
  (created 2026-11-08 with CreateFunctionContaining; body
  0x004E3F30-0x004E407F). Gates (read): flags bits 5 and 11 off; for base
  form types 0x1A, 0x23, 0x24 (light, NPC, creature by xOBSE's FormType
  table, unverified here) vfunc +0xFC first; then the node from vfunc
  +0x154, its first child, that child's controller (+0x0C), 0x00560920 (a
  NiRTTI cast, see below) to NiControllerManager; with its flag bit 3 set the
  sequences at +0x40 (count ushort +0x46) are checked with 0x0049F950 and
  stopped (0x004E0D90, 0x006C9CB0, 0x004D90D0); then vfunc +0x164 and, when
  the reference's +0x3C object has a +0x1C, 0x00476D10(ref, frameSeconds,
  [0x00A30634]) advances its animation.
- The hot instructions are the gate's loads (0x004E3F8E, 0x004E3F9C,
  0x004E3FA7: node, its children array, the first child, its controller -
  measured 96 % of the function's samples): four dependent pointer loads
  into cold memory per reference per frame, for every reference of every
  loaded cell. The animation advance itself is not what costs.
- Caller 0x004CCC50 **TESObjectCELL::UpdateReferenceAnimations (proposal)**:
  under the cell's lock (0x00496EA0/0x00496F50), when cellProcessLevel == 6,
  for each entry of the objectList (+0x48): when !IsActor() (vfunc +0x190)
  or [ref+0x78] != 0 or 0x0045A500(), vfunc +0x160 (read). Reached from
  GridCellArray::Update's 0x004CCCE0? No: w1 of every sample is 0x004823D0
  and 0x004CCCE0 is a plain tail into the Havok world (read), so the call
  into 0x004CCC50 per cell comes from GridCellArray::Update through a path
  the scan does not show (unverified; 0x004D4970 is the visible one).
- 0x00560920 **NiRTTI cast (`NiDynamicCast(rtti, object)`; proposal)**: the
  object's vfunc +0x04 gives its NiRTTI, the chain is walked through +0x04
  (base) until the wanted one or null (read); 111 callers; 0.5 %.

### The CRT square root's slow path: 1.7 % outside, 1.2 % inside the pass, about 0.2 ms a frame

Read in `re/export/frame-update/0x00982C30`, `0x00982C4D`, `0x00991B88`,
`0x00991BAB`, `0x00991AB7`, `0x0099E2B9` (asm):

- 0x00982C30 `_CIsqrt` (145 call sites in the exe): stores the double,
  0x00991B88 tests the exponent for inf/NaN, 0x00982C4D does the work:
  FSTCW; when the control word is not 0x027F, 0x00991B15 loads
  (CW & 0x0300) | 0x007F - the precision control kept, round to nearest,
  all exceptions masked (as knowledge/frame-clock.md recorded); FSQRT for a
  non-negative argument; then, with the dword 0x00BA9DDC zero (the default
  in this build, read as the image's value), `MOV EDX,5; LEA ECX,[0x00B30B40]
  ("sqrt", the matherr name); JMP 0x00991BAB`.
- 0x00991BAB: control word 0x027F -> return at once. Otherwise, when the
  caller's PM bit (0x20, the inexact-result mask) is set: FSTSW, and when the
  status word's PE flag (0x20, inexact) is set, `MOV EAX,8; CALL 0x00991AB7`
  -> __87except(5, record, &cw) 0x0099E2B9: __handle_exc(0x10, ...),
  __ctrlfp(cw, 0xFFFF), __set_errno_from_matherr(8). The result ST0 is
  saved and reloaded around it (0x00991AC0 / 0x00991AE3).
- The game's control word is 0x007F or 0x0C7F (measured,
  knowledge/frame-clock.md), never 0x027F, and its PM bit is set. So **every
  CRT square root whose result is inexact - nearly all of them - runs the
  exception-reporting path**: exception record, control word restore, errno.
  Measured share: 1.7 % of the out-of-pass samples (callers by w0 right
  after a CALL 0x00982C30: NiBound::Merge 0x0072A7FC and 0x0072A754,
  0x004D7E7B; other named words are stale) and 1.2 % of the in-pass samples
  (BSFadeNode::OnVisible 0x004A0A5A / 0x004A0CF2, 0x007F0C17, 0x007016B1).
  The same share in the dual-pass run (1.7 %) and 1.0 % in the carry-off run.
  Derived: about 0.2 ms a frame, on top of the 0.08 ms the square roots
  themselves take (0.6 % / 0.5 % in 0x00982C30 and 0x00991B15).

### Actors, the HUD, sound, scripts (the long tail named)

| Address | Proposed name | Share | Read |
|---|---|---|---|
| 0x00678510 | ProcessManager::Update (0x0040D800's "actor process manager 0x00B3BD00") | - | debug line numbers 0x6E..0xBE into 0x00B3BCF4; 0x00673B10(frameSeconds); a random draw 0x009828C0 against 50 and 10 and the accumulator at +0x24 decide whether the low/middle updates run this frame; 0x00673C10 / 0x00673E90 / 0x00674200 / 0x00677EC0 gated on the bytes +0xA0..+0xA3 and 0x00B333B8. |
| 0x00673C10 | ProcessManager::UpdateHighProcesses (proposal) | 0.5 % | walks the list from 0x007616D0, per actor: flags bits 21/11/5, process level 3 through 0x00659A00, IsActor vfunc +0x190, the dead/disabled tests (+0x1F0, +0x334), the actor update vfunc +0x1C0(time), the respawn/remove branch with 0x0067B320/0x0067B260; a FormHeap list of deferred removals freed at the end. |
| 0x00659A00 | Actor::GetProcessLevel (proposal) | 0.8 % | [this+0x58] (the process) -> vfunc +0x08, else -1 (eight instructions, 38 callers); hot through the call count. |
| 0x005E1DB0 | actor base-flag test (unnamed) | 0.7 % | base form from vfunc +0x170 when IsActor; returns !((base+0x28 >> 9) & 1). Which flag bit 9 of TESActorBaseData is: unverified. |
| 0x005E6110 | actor state test (unnamed) | 0.2 % | vfunc +0x380 == 0 and the process's +0x36C, or the process's +0x08 object's byte +0x20 in {12, 13}. |
| 0x005A6DE0 | HUDMainMenu compass update (proposal) | 0.6 % | strings "hudmain_compass_icon", "Bquest_%i", "player_target_%i"; the hot loop 0x005A6F97-0x005A6FC4 walks a list of references each frame, tests flag bit 11 of each, asks vfunc +0x174 (the position) and subtracts the player's: the compass markers' distances, every frame. |
| 0x006AABD0 | WAV file reader (proposal) | 0.6 % | mmio with a custom IO procedure 0x006AAAA0 ("data\\sound\\", "Attempting to play %i channel sound ... (must be mono)"); the hot loop 0x006AAFC0-0x006AB001 copies the file **byte by byte** into the destination (MOV AL,[EDX]; MOV [EDI+EBX],AL) with a refill call per exhausted buffer. Sound files are read on the render thread, one byte at a time; a hitch source rather than a steady cost. Caller 0x006AC610 (the sound file open: ".\\data", "voice"). |
| 0x00516830, 0x004FA110, 0x004FBE00 | the script runner ("Unknown reference object for function '%s'", "Variable ID %08X not found", "fQuestDelayTime") | 0.5 % | quest and object scripts ticking. |
| 0x0041E210 | BaseExtraList::GetByType(UInt8 type) | 0.4 % | the type bit in the presence mask at +0x08 first; a TLS cache ([tls+0x08] == this -> [tls+0x10+type*4]); else the list at +0x04 under the named lock 0x00401020("BaseExtraList::GetExtraData") / 0x00401040 with a move-to-front 0x0041DF90. 215 callers. |
| 0x0055E000 | NiTPointerMap::Lookup (xOBSE seed) | 0.2 % | hash through vfunc +0x04, bucket walk, compare through +0x08. |
| 0x00402480 | MemoryHeap allocation slow path ("MemoryHeap::MemoryProblem") | 0.3 % | called from kMemoryHeap_Allocate 0x00401AA0. |
| 0x0073A5E0 | NiTArray::AddUnique (proposal) | 0.3 % (0.7 % in pass) | linear search, grow by doubling through 0x006E8CA0, append. |
| 0x00707530 | NiAVObject::GetProperty(int type) | 0.3 % (1.1 % in pass) | the property list at +0x9C, each property's vfunc +0x4C compared with the type (<= 9). |
| 0x0070AB40 / 0x007073D0 | NiNode::OnVisible(culler) / NiAVObject::Cull | - / 2.4 % in pass | every child with a non-null pointer through 0x007073D0(culler), skipped when the node's radius is 0. The in-pass NiNode walk of docs/subsystems/single-pass-stereo.md. |
| 0x004A0920 | BSFadeNode::OnVisible (proposal) | 0.1 % (0.8 % in pass) | distance to the camera (sqrt), the fade distances per node type at +0xEC (settings 0x00B07624 x 0x00B075EC etc., gated on the byte 0x00B0727C), the fade value +0xE8, the alpha property through NiObjectNET::GetExtraData 0x006FF9C0 (0.5 % with its name getter 0x00452A60), then NiNode::OnVisible. |
| 0x006CA950 / 0x006C6DC0 | NiControllerSequence::Update / its per-interpolator writer (proposals) | 0.4 % | ease-in/out state machine on +0x44..+0x50; the writer sets three values per interpolator slot (+0x14 array, 0x18 stride) and marks them (-FLT_MAX sentinels from 0x00A7DEB4). |

## What this says about the 90 Hz question (derived)

At 13.4 ms a frame and 2.6 ms of pose wait (perf-phases, same run), the
render thread works about 10.8 ms; at 90 Hz the budget is 11.1 ms. The
out-of-pass work splits into: scene graph update 1.6 ms, Havok 1.0 ms,
out-of-bracket render passes with their lock waits 0.4 ms plus their
culling, the square root's slow path 0.2 ms, the reference animation gate
0.26 ms, actors 0.2 ms, the rest a long tail of game code (about 2 ms).
None of it is one function; the three largest blocks are Gamebryo's update
walk, Havok and the long tail.

## Levers, with their costs (usage share, not dollars)

1. **The square root's slow path** (0.2 ms a frame, 1.5 %): a replacement
   for 0x00982C30 that reproduces its result bit for bit (FSQRT under round
   to nearest with the caller's precision control, the NaN / negative /
   infinite cases as the original handles them) without the exception
   record. Small (about 0.3-0.5 M tokens): a detour at one entry, emulator
   cases under the four control words (tools/difftest), a shadow run
   comparing results. Risk: whether any code reads errno after sqrt
   (unread; the slow path sets it) and the 0x00BA9DDC flag's other path.
   The cheapest lever here and the only one that is verifiable without the
   headset.
2. **The reference animation gate** (0.26 ms): a per-reference "has a
   controller manager" bit kept with the 3D (set on load, cleared on 3D
   release) so 0x004E3F30's four cold loads run only for animated
   references. Medium (0.6-1 M tokens): the writers of the reference's 3D
   must be mapped (0x004E3490 and its siblings), the bit's invalidation
   proven, then a shadow comparing the original's decision with the bit's.
3. **Havok's 1.0 ms**: first the attribution run (cheap, one harness run,
   about 50 k tokens) with OBVR's hand bodies off, if OBVR offers the
   switch; a rebuild of anything in Havok is huge and off the table.
4. **The scene graph update** (1.6 ms): the largest block and Gamebryo's
   core. Before any design, a count probe (cheap, about 0.1 M tokens): how
   many nodes UpdateDownwardPass visits per frame and how many of
   UpdateObjectControllers' calls find an empty list. The lever that counts
   is skipping unchanged subtrees, which is a design for a later slice.
5. **The out-of-bracket passes' lock wait** (0.4 ms): OBVR's profiler can
   bracket the passes it does not bracket yet; which pass waits is their
   measurement.
6. The sound reader's byte copy and the compass scan are hitch and tail
   items, not frame-rate levers.

## Sources

- Sampler and runs: `../OBVR/src/render/RenderSampler.h`,
  `artifacts/shadow/obng-profile-gold-road-single/20261108-133456/OBVR-performance-1-2/samples.csv`,
  the dual run `…-dual/20261108-134233`, the carry-off repeat `…-single/20261108-140426`.
- Decompilates: `re/export/frame-update/` (Ghidra 12.1.4, the project
  `~/tools/ghidra-projects/OBNG`), 95 functions exported 2026-11-08.
- xOBSE layouts: deepwiki llde/xOBSE, 2026-11-08 (TESObjectCELL +0x24
  flags0, +0x26 cellProcessLevel, +0x48 objectList, +0x54 NiNode;
  GridCellArray +0x0C size, +0x10 grid; TESObjectREFR vtable +0x154 Unk_55,
  +0x160 Unk_58, +0x170 GetBaseForm, +0x190 IsActor, +0x1C0 Unk_70,
  +0x1F0 Unk_7C).
- OBVR's seeds: `../OBVR/src/game/GameAddresses.h` lines 364, 1259, 1731.

## Lever 1 built: the square root's dispatch (2026-11-08)

The replacement for 0x00982C30 exists (plugin/src/FastSqrtLogic.h,
FastSqrt.cpp; re/functions/0x00982C30.md; docs/subsystems/crt-square-root.md):
133 bytes of generated code that make the original's decisions - the stored
high dword's sign and exponent, the control word's masks and precision
control - and run FSQRT under the original's word for the common case,
jumping to the original through the trampoline for every other. What was
learned building it, all read or measured on 2026-11-08:

- The original's INEXACT path writes nothing a caller can see: __handle_exc
  re-raises the PE flag that FSQRT already set (through a deliberately
  inexact FLDPI/FSTP), __ctrlfp puts the caller's word back,
  __set_errno_from_matherr(8) returns without touching errno (only DOMAIN
  and the range types write it). The errno risk named under lever 1 is
  closed for the fast path; the DOMAIN cases (negative, NaN) stay with the
  original and keep writing EDOM.
- The original clobbers EDX (the FSTCW over the pushed EDX slot) as well as
  EAX and ECX; the dispatch keeps ECX and EDX.
- Precision control reaches FSQRT on the hardware: every root the game's
  callers get has 24 significant bits (sqrt(2) = 0xB504F3 << 40 at 0x007F
  and 0x0C7F alike, since the original rounds to nearest whatever the
  caller's mode). Unicorn's FSQRT ignores PC, so the emulator cannot gate
  this function; the original's bytes run on the host instead
  (FastSqrtOracleTest: 1.8 million calls, six words, 0 differences in
  result, control word, exception flags, stack top, errno writes).
- The condition codes C0..C3 after the call differ from the original's on
  about 29 % of PC-24 calls (the original's FLDPI/FSTP changes C1 after the
  root). No caller reads them: a square root's caller continues with FSTP,
  FMUL or FCOM, and FCOM/FUCOM set them afresh. Reported in the test, not
  asserted.
- The in-game gate ([Shadow] Sqrt=1, tools/hand-scripts/obng-sqrt-shadow.ini)
  and the Gold Road measurement ([Route] Sqrt=1,
  tools/hand-scripts/obng-measure-profile-sqrt.ini against
  obng-measure-profile.ini) wait for the Linux game.

## Runs of 2026-11-08 17:48-18:04: the square root verified and routed, the hand bodies acquitted

Four Gold Road single-pass runs in a row on the Linux game (OBNG.dll 4a2f6474
from master 006748a, OBVR.dll as deployed, save OBVRHarnessMOO; artifacts
under artifacts/shadow/obng-profile-gold-road-single/20261108-174827 (shadow),
-175231 (baseline), -180031 (routed) and obng-profile-gold-road-nobodies/
20261108-175632). All four harness verdicts PASS. Samples below are the render
sampler's out-of-pass samples in the window s2-y0..s2-y45 (about 9.6 s, one
sample a millisecond) unless said otherwise.

### The square root's dispatch: verified in the game, the slow path gone

- Shadow gate ([Shadow] Sqrt=1, tools/hand-scripts/obng-sqrt-shadow.ini):
  **65 443 293 calls compared, 0 mismatches** in result, control word and
  exception flags over the whole scenario (measured, OBNG.log of run
  -174827). About 330 000 _CIsqrt calls a second, 4 400 a frame at 75 Hz
  (derived from the 10 s summaries). The ledger's state is **verified**.
- Routed ([Route] Sqrt=1, obng-measure-profile-sqrt.ini) against the
  baseline of four minutes earlier (measured):

| | baseline -175231 | routed -180031 |
|---|---:|---:|
| samples in the window | 9 616 | 9 781 |
| samples in the CRT error path (0x00991BAB, 0x00991AB7, __87except, __ctrlfp, __handle_exc, 0x00991B15) | 181 | **0** |
| CRT samples out of the pass | 395 | 218 |
| of them __ctrlfp / 0x00991B15 / 0x00991BAB / __87except | 72 / 57 / 23 / 17 | none in the top eight |
| CRT samples in the pass | 255 | 204 |
| of them 0x00991B15 / __ctrlfp / 0x00991BAB | 25 / 23 / 6 | none |
| the dispatch's own code (module "?") | - | 2 |

  Derived: the chain took about 230 of 9 600 samples a window in the
  baseline, 2.4 % of the thread, about 0.3 ms a 13.3 ms frame; the routed run
  has none of it, and the dispatch itself is below the sampler's resolution.
  The frame time cannot show the gain here: the Linux harness runs at 75 Hz
  with vsync, every phase's mean frame is 13.3-13.5 ms in both runs and the
  pose wait absorbs the difference (and the scene load differs between runs
  by 15-20 %: game code 2 234 against 2 519 samples, NiMain 1 358 against
  1 626). The 90 Hz question is the Windows run's.
- What remains of the CRT out of the pass (218 samples): 0x00990BA7 (36),
  0x009828C0 (32, the random generator of ProcessManager::Update),
  0x00984012 (23), 0x009812C0 (17), 0x0099358E (14), 0x0099098A (14),
  0x00982525 (13), 0x0098DF50 (12). 0x0099098A and 0x00990BA7 test the same
  0x00BA9DDC flag and the same PE bit as the square root's exit (read in
  their decompilates): another x87 math helper's INEXACT path (which
  function it belongs to: unread). A smaller lever of the same kind, about
  0.05 ms (derived); worth a look only after the Windows run.

### Havok: OBVR's hand bodies are not the cause

Baseline against the same scenario with `[Hands] BodyCollision=0`
(obng-profile-gold-road-nobodies.txt; PhysicsRate=0 in both; measured):

| run | Havok samples (0x889000-0x92FFFF) | share of the window | GSK 0x0093C690 | supporting vertex 0x00916C00 |
|---|---:|---:|---:|---:|
| baseline -175231, bodies on | 771 | 8.0 % | 157 | 139 |
| nobodies -175632, bodies off | 1 074 | 11.1 % | 257 | 188 |
| reference -133844, bodies on (earlier build) | 1 028 | 9.9 % | | |
| reference -140426, bodies on (earlier build) | 337 | 3.2 % | | |

Without the hand and weapon bodies the Havok work did not fall; it rose, and
the spread between runs with identical settings (337 to 1 028) is wider than
any effect the bodies could have. The nobodies run carried 20-40 % more of
everything (game code, NiMain, CRT), i.e. more scene load, not more physics
for its own sake. **Derived: the hand bodies are acquitted** (one run per
side, inside the spread). What drives the GSK narrowphase stays open between
the engine's own bodies and OblivionWalkers' colliders (OblivionWalkers.dll
was loaded in all four runs); the next cheap decider is the same pair with
Walkers' colliders off, if Walkers has that switch.

### Havok, closed as far as plugins go (2026-11-08 18:30)

OblivionWalkers (its session, read in its source: grep havok/bhk/collid)
creates no Havok bodies of its own: the walkers are ordinary engine
creatures with the engine's character controller and ragdoll; the mod
touches Havok only to change corpses' collision filter word ([Rise]
KeepCorpseApart) and to push ragdoll bodies during the seconds a walker
rises. With OBVR's hand bodies acquitted above, **the Havok work on the Gold
Road is the engine's own physics**: the creatures' and the player's
controllers against the terrain's MOPP and the statics (derived from the two
findings; the per-body split is unmeasured). The share the extra creatures
cost could be measured with OblivionWalkers.ini `[Replace] Enabled=0` (the
live INI under Data/OBSE/Plugins, not a hand-script `ini` line, which
reaches OBVR's test INI only) on a cell with many walkers - a scene change,
so a cost of the creatures, not a bug to fix. Not run.

### The scene graph update, read closer (2026-11-08 18:30; baseline run -175231, window s2-y0..s2-y45)

1 537 of the window's 9 616 out-of-pass samples (16 %, about 1.5 ms a frame)
sit in the update walk. By instruction (measured eips, read in the asm):

| Where | Samples | What the hot instructions are |
|---|---:|---|
| 0x0047C930 UpdateObjectControllers | 354 | the loads of the property list head (+0x9C), each list node's property and the property's controller (+0x0C): pointer chasing through cold memory. Callers: 0x007075B0 (selected pass, leaf) 142, 0x00707580 (full pass, leaf) 105, 0x0070A0D0 (full pass, node) 63, 0x0070A190 (selected pass, node) 40. |
| 0x0070A0D0 NiNode::UpdateDownwardPass | 162 | the child pointer and the child's vtable (0x0070A119 / 0x0070A11F): the first touch of each child. |
| 0x0070A190 NiNode::UpdateSelectedDownwardPass | 144 | the child's flags word (0x0070A1F1): the first touch of each child. |
| 0x0070C120 UpdateWorldData | 118 | the 13-dword copy of the composed transform and the collision object's load. |
| 0x0053D7A0, 0x007100A0, 0x007101F0 (transform compose, matrix multiply, point transform) | 386 | the arithmetic itself. |
| 0x0072A6B0, 0x0072A820, 0x0072BB30, 0x0070A360 (bounds: merge, transform, skin, node) | 309 | the arithmetic and the merges' square roots (the roots' CRT path counted under the CRT above). |
| 0x00607840 (bhkRigidBody in a world) from 0x0088EDC0 | 54 | two dependent loads into the Havok body. |

So about half of the walk is memory latency on first touches (lists,
children, flags, bodies), a quarter transform arithmetic, a fifth bounds.
How the engine already limits the walk (read): the flags byte +0x18 carries
Gamebryo's selective-update bits - bit 1 "visit this child", bit 2 "update
transforms", bit 3 "update controllers", bit 4 "rigid" (0x0070A190 reads
them; 0x0070A190 and 0x007075B0 pass bit 3 to UpdateObjectControllers,
which returns at once when it is clear). The full pass (0x0070A0D0,
0x00707580, reached through NiAVObject::Update 0x00707370 from the actor
update 0x00471F20) passes 1 and walks every object's lists regardless.

Levers that keep the outputs bit for bit (derived):
- **Skip the controller walk in the full pass for objects without
  controllers** - 168 of the 354 samples come from the full-pass callers,
  about 0.17 ms. Needs a reliable "has controllers" bit: Gamebryo's bit 3 is
  set by SetSelectiveUpdateFlags from the actual controllers, but whether
  Oblivion keeps it current for every object the full pass visits is
  **unread**; with a stale bit the skip would drop a controller update.
  Medium (0.5-0.8 M tokens: read the maintainers of bit 3, a shadow that
  compares world transforms after the walk, the route).
- **Fewer first touches** (the other 0.6 ms): a layout or prefetch change,
  not a code change - out of reach of a detour.
- **The arithmetic** (0.7 ms): Gamebryo-inherent per visited object; a SIMD
  rebuild would gain a fraction of it at large cost. Not recommended.
Neither is worth starting before the Windows run says whether the CPU bounds
the frame at 90 Hz.

## The Windows capture of 2026-11-07 23:45: at 90 Hz the GPU bounds the frame, not the CPU (read 2026-11-08)

The capture Nadi's live play left on the Windows disk
(C:\Steam\steamapps\common\Oblivion\OBVR-performance-1-31, mounted under
/run/media/nadi/Programs; its logs in artifacts/windows/20261107-2345):
Dream Air at 90 Hz (11.11 ms a period), single pass through OBNG's route,
4036x3376 a pixel per eye (the double-wide target 8072x3376, 4x MSAA by the
scene target probe), 10 363 frames over about 160 s, no hand-script marks,
GpuTiming off - but SteamVR's compositor timing (compositor.csv:
pre_submit_gpu_ms, total_render_gpu_ms) is the GPU's own clock. All measured:

| | whole capture | the light stretch (0-50 s) | the heavy stretch (70-110 s) |
|---|---:|---:|---:|
| frame interval mean / median / p95 | 15.4 / 11.4 / 30.7 ms | 11.5-12.1 / - / 12.3-22.8 ms | 21.3-28.4 / - / 33-34 ms |
| frames over 11.6 ms (a missed vsync) | 43.5 % | | |
| frames over 22 ms (half rate) | 21.0 % | | |
| poses_wait mean (the CPU idle before the poses) | 3.6 ms | 2.3-3.9 ms | 3.5-5.6 ms |
| GPU before submit, compositor's clock, mean / p95 | 7.0 / 13.6 ms | 5.2-6.9 / 5.7-11.1 ms | 12.5-15.2 / 17.7-22.4 ms |
| GPU total render (app + compositor) mean / p95 | 8.9 / 18.8 ms | | |
| compositor: mis-presented | 5 185 of 10 351 | | |

SteamVR fell into interleaved reprojection (every frame shown twice) again
and again (OBVR.log "Compositor: SteamVR shows every frame twice"). In the
heavy stretch the GPU alone needs 12-15 ms a frame, more than the period;
the CPU meanwhile waits 3.5-5.6 ms for the poses. In the light stretch both
fit (CPU work about 8-9.5 ms, GPU 5-7 ms) and the frame holds 11.1 ms.
**Derived: on the Dream Air at this resolution the GPU bounds the frame
wherever it drops; the CPU has 2-5 ms of headroom there.** The CPU levers
of this file (the square root's 0.3 ms, the scene graph's 1.5 ms) do not
buy 90 Hz on Windows; they matter for the Linux Beyond at 75 Hz, where the
CPU side sits at 11 ms, and as headroom.

The render thread's samples on Windows (the sampler ran): out of the pass
58 % ntdll (the waits), game code 12.8 %, NiMain 7.3 %, OBVR.dll 4.7 %,
Havok 4.7 %, CRT 3.5 % (the square root's error path 0.7 %); in the pass
renderer/BS 26.6 %, D3D9.DLL 18.7 % (the native driver), OBNG.dll 9.1 %,
OBVR.dll 8.7 %, OblivionFrameFixes.dll 7.5 %, CRT 6.9 % (the square root's
path 1.0 %), FoliageAA.dll 3.1 %. OBNG's own in-pass share (the eye switch
per draw) is about 0.6 ms of a 6.8 ms pass (derived) - a CPU cost to trim
when the CPU is the limit, not now.

What moves the GPU (candidates, not measured): the render scale (4036x3376
is SteamVR's 100 % for the Dream Air - Nadi 2026-11-08; the lens distortion
makes the recommended target larger than the 2560x2560 panel - so the lever
is a scale below 100 % where the GPU needs it, which is what OBVR's dynamic
resolution does: DynamicResolution=0 in the shipped INI, Min 0.5, Target
0.85; Oblivion Performance 2026 is measuring it on the Gold Road today), the
game's own 4x MSAA on a 13.6-million pixel eye (Oblivion.ini iMultiSample=4
on both machines, read; OBVR's SceneFrame.h records that with iMultiSample=0
the game does not draw the world into the multisampled back buffer and
OBVR's stereo then showed one picture for both eyes, so 0 is not an option
and 2 is untested), and the shaders' fill cost (the grass fix's shaders,
FoliageAA's own grass multisampling, separate from the back buffer).
The Linux harness cannot stand in for this: its Beyond target is 2568x2568
(SteamVR supersampleScale 0.52), half the pixels, at 75 Hz.

## iMultiSample 4 / 0 / 2 on the Gold Road (2026-11-08 19:31-19:44, Linux, Beyond 2568x2568)

Three runs of tools/hand-scripts/obng-profile-gold-road-eyedump.txt (the
profile scenario plus OBVR's eye dump at every mark) with the Linux
Oblivion.ini's iMultiSample at 4, 0 and 2 (backed up and put back;
artifacts/shadow/obng-profile-gold-road-eyedump/20261108-193145, -193550,
-193956; OBNG.dll 454727e6, OBVR.dll 31ef3ad7). Measured:

| iMultiSample | stereo path | eye pictures (tools/eye-shift.py, five marks, six bands) | frame mean at spot 2 | frames over 20 ms at spot 2 |
|---|---|---|---:|---:|
| 4 | single pass through OBNG ("OBNG took the eyes ok") | far bands 101-104 px, near 110-118 px | 13.7-14.0 ms | 1.0-1.3 % |
| 0 | **dual pass** - OBVR never hands the eyes over (no "Single-pass stereo" line; 0 eye switches) | the same shifts as with 4 (102 / 117 / 110 / 112 ...) - correct stereo | 18.6-21.1 ms | 31-53 % |
| 2 | single pass through OBNG | the same shifts as with 4 - correct stereo | 16.1-25.1 ms | 16-47 % |

- **Why 0 is the dual pass** (read in OBVR's src/render/ObngStereoLogic.h):
  `WorldDrawnIntoBackBuffer = !imageSpaceEffects || effectiveSamples >= 2`
  is one of WorthSettingEyes' conditions; with image-space effects on and no
  multisampling the engine draws the world into a frame-sized texture and
  copies it (SceneFrame.h), and OBVR keeps the single pass to the back-buffer
  path. VR works with 0, with the correct picture, at the dual pass's CPU
  cost (two world renders a frame). A player who sets iMultiSample=0 for
  the GPU's sake gets the CPU-heavier path without being told.
- **The GPU cost of the sample count could not be read from these runs.**
  Both the 0 and the 2 run stutter heavily wherever the scenario first
  reaches a place (31-53 % of the frames over 20 ms at spot 2, maxima of
  0.4-1.2 s), while the 4 run of the same minute does not (1 %), and the
  GPU sample means of the stuttering phases are garbage (negative numbers
  from broken timestamp pairs). Derived, unverified: DXVK compiles every
  pipeline anew for a sample count it has not seen (its state cache holds
  the 4x pipelines of all earlier runs), and the first run under a new
  count pays for it. The decider is a repeat run of 2 (and 0) with the
  cache warm; not run (the game was queued). On the Dream Air the question
  is better asked on Windows anyway (the native driver, no pipeline cache
  of this kind): iMultiSample=2 there, the same place, the compositor's
  pre_submit_gpu_ms against the 4x capture of 2026-11-07.
- What this settles: **iMultiSample=2 is a correct single-pass
  configuration** (the eye pictures equal the 4x run's shift for shift), so
  it can be tried on Windows for the GPU without a stereo risk; 0 is
  correct too but costs the single pass. Whether OBVR could allow the
  single pass on the texture path (one world render into the double-wide
  texture, one copy) is OBVR's question; the gate is deliberate and the
  texture path with a side-by-side frame is unverified.

## The update tree counted (2026-11-08 20:10, [Probe] SceneUpdate, run obng-profile-gold-road-single/20261108-200948)

OBNG.dll 454727e6 with the scene update probe (plugin/src/Probes.cpp: eight
entry detours, the two drivers timed by RDTSC, the tree's functions counted),
the Gold Road single-pass profile scenario, the route on, OBVR.dll 31ef3ad7,
OblivionWalkers on. Per grid update (one a frame), medians of the 10 s
windows at each spot (measured):

| spot | cells | NiNode::UpdateDownwardPass | selected | leaves | UpdateWorldData | controller walks | of them idle | NiAVObject::Update calls | GridCellArray::Update ms | NiAVObject::Update ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 (-33 -4) | 17 | 6 150 - 7 450 | 400 - 1 800 | 930 - 1 270 | 9 100 - 12 300 | 6 100 - 9 500 | 39 - 47 % | 93 | 1.08 | 1.23 |
| 2 (-27 -3) | 17 | 8 000 - 9 200 | 770 - 1 400 | 1 370 - 1 490 | 11 100 - 13 600 | 8 400 - 10 600 | 47 - 49 % | 125 | 0.96 | 1.77 |
| 3 (-21 -1) | 17 | 4 500 - 5 200 | 600 - 1 200 | 440 - 490 | 6 200 - 8 100 | 3 600 - 5 100 | 32 - 38 % | 69 | 1.10 | 0.73 |

- **Idle controller walks** (no property list to walk, no controller): a
  third to a half of the 4 000 to 10 000 walks a frame find nothing. At
  tens of nanoseconds a walk that is 0.1 ms a frame at most (derived): not
  a lever on its own.
- **The grid update's cost is the count, not the node**: about 1.0 ms a
  frame over roughly 20 000 node visits and world-transform recomputes
  (derived: 50 ns a visit). Seventeen cells a frame (the interior and the
  checkerboard half of the borders). The lever that counts here is fewer
  visits - the neighbour cells' static subtrees every second or fourth
  frame, or dirty tracking Gamebryo does not have - medium to large, with
  animated objects in neighbour cells at half rate as the risk.
- **The actors' explicit updates are the larger half**: 70 to 125
  NiAVObject::Update calls a frame (0x00707370, the actor update's
  0x00471F20 among the callers) take 0.7 to 1.8 ms, 10 to 14 us each - a
  skeleton's downward pass with a transform compose per bone (derived from
  the per-call time; the callers are not split by this probe). An actor
  update LOD (distant actors every second frame with the summed time) is
  the lever: medium to large; OBVR's body IK and hit tests read bones every
  frame and would need to follow.
- **Caveat on the times**: the counted detours sit inside the timed
  drivers; some 30 000 counted calls a frame at 10 to 20 ns each inflate
  the two times by about 0.3 to 0.6 ms together (derived). The sampler's
  1.6 ms for the whole update area (above) is the lower reading; the split
  grid against actors is the probe's finding.
- The pair with OblivionWalkers' Rise and Replace off (run -201343) was
  interrupted after 48 s (KeyboardInterrupt in the runner, SteamVR was
  shut down around then); no result. Walkers places no Havok bodies of its
  own (Walkers 2026-11-08), so the Havok question is the engine's own
  creatures and the player; open.


### iMultiSample=0 runs the single pass since OBVR.dll 7d8b9397, and what the samples cost on the GPU (2026-11-08 20:24-20:36)

OBVR built the single pass for the texture path within the evening (its
runs in artifacts/shadow/obng-profile-gold-road-eyedump/20261108-202413 and
-202826 with iMultiSample=0, -203233 with 4; OBVR.dll 7d8b9397 on both
machines; the Linux INI put back to 4). Measured:

| | iMultiSample=0, first run | iMultiSample=0, second run | iMultiSample=4 |
|---|---:|---:|---:|
| stereo path | single pass (eyes taken) | single pass | single pass |
| eye disparity, far / near bands | 101-103 / 110-115 px | 101-103 / 110-115 px | 101-104 / 110-118 px |
| GPU before submit, mean, the four later phases | 5.7 / 6.1 / 6.0 / 6.7 ms | 5.6 / 5.7 / 5.7 / 4.9 ms | 6.9 / 6.9 / 6.5 / 5.4 ms |
| GPU p95, the same phases | 6.8 / 7.4 / 7.3 / 8.3 ms | 6.7 / 7.0 / 7.3 / 6.0 ms | 8.0 / 8.0 / 7.9 / 6.6 ms |
| frame mean, every phase | 13.6-14.6 ms | 13.5-14.2 ms | 13.6-14.0 ms |
| frames over 20 ms | 1.1-2.3 % | 0.6-1.6 % | 0.6-1.3 % |

- **The stereo is correct without multisampling** (the disparities equal the
  4x run's band for band) and the single pass now runs there; the gate of
  the earlier section is lifted in 7d8b9397.
- **4x MSAA costs about 1.0-1.2 ms of GPU a frame on the Beyond at 2568x2568
  an eye, about 17 %** (derived from the two means of the same minute;
  frames vsync-bound at 75 Hz, so the frame time itself does not move).
  Carried to the Dream Air's 12-15 ms heavy stretch at the same share,
  MSAA 0 alone would land at about 10-12.5 ms: on the edge of 11.1 ms, not
  safely under it. The dynamic resolution stays necessary; the two
  together are the GPU plan. Windows measures the real share.
- The stutter of the 19:35 (0) and 19:40 (2) runs is **not** explained by
  pipeline compilation: OBVR's first 0 run of the evening was as smooth as
  its second (1.1-2.3 % over 20 ms). Cause unknown; both stuttering runs
  came right after a different OBNG.dll had been deployed (454727e6 at
  19:31) and inside a busy half hour of harness runs by four sessions. A
  repeat of iMultiSample=2 is still owed before 2 can be costed.

## The Gold Road on Windows at 90 Hz (runs of 2026-11-09 07:59-09:06, read 2026-11-09)

Nadi ran the profile scenario on Windows this morning, headless (the headset
not worn, SteamVR reprojecting every frame, so the frame time is pinned to the
11.11 ms vsync and only the per-frame work and the GPU time carry
information). OBVR's session left the per-phase table in
../OBVR/artifacts/hand-script/obng-profile-gold-road-single/windows-2026-11-09-results.md;
the captures stayed on the Windows disk (OBVR-performance-1-34..38 beside
Oblivion.exe). Cross-checked here with perf-phases.py on 1-36 (run 081105,
spots 1 and 2) and 1-38 (run 090057, spot 3), and the render sampler read.
Measured unless marked:

| phase (Dream Air 90 Hz, 4036x3376 an eye) | pose wait mean | scene pass mean / p95 | GPU before submit mean / p95 |
|---|---:|---:|---:|
| spot 1, eight headings | 4.2 ms | 2.2 / 2.9 ms | 2.5 / 3.0 ms |
| spot 2, eight headings | 1.7 ms | 2.9 / 3.5 ms | 5.2 / 5.8 ms |
| spot 2, the walk | 1.1 ms | 3.3 / 3.8 ms | 5.0 / 5.5 ms |
| spot 3, eight headings | 5.9 ms | 3.8 / 5.5 ms | 4.6 / 5.2 ms |

- **The GPU is far under the period on the Gold Road**: 2.5-5.4 ms of
  11.1. The CPU is the nearer limit at spot 2: 11.1 ms frame minus 1.1-1.7
  ms of pose wait is 9.4-10 ms of work a frame (derived), 85-90 % of the
  budget, with the headset not worn.
- **The sample count was 2, not 0**: both run logs say "back buffer image
  8072x3376 ... samples=2" (OBVR.log of 081105 and 090057), and the Windows
  Oblivion.ini holds iMultiSample=2. The results note says iMultiSample=0;
  the logs win. So these runs do not measure the texture path's single
  pass on Windows; they measure MSAA 2 on the back-buffer path.
- **The square root's route was off**: the Windows OBNG.ini had no Sqrt key
  (default 0; read 2026-11-09, both the file and its 2026-11-08 21:12
  backup). The OBNG.dll there is the build of 2026-11-08 17:48 (md5
  d0f87c89...), which knows the key: its log prints "Route RangeWalk=0
  Sqrt=0". The evening captures of 2026-11-08 (1-32, 1-33) ran with the
  route off as well: the CRT error path has 137 and 158 of their 60 530 and
  55 645 samples (the routed Linux run -180031 has 0). The Windows pair
  with and without the route does not exist yet; `Sqrt=1` was added under
  [Route] in the Windows OBNG.ini on 2026-11-09 (backup
  OBNG.ini.bak-2026-11-09) for Nadi's run. The sampler shows
  the CRT error path at 2.7 % of the out-of-pass and 1.5 % of the in-pass
  samples at spot 2 (window s2-y0..s2-y45, 8 143 samples) - about 0.3 ms a
  frame still to gain on Windows by one INI line.
- The out-of-pass shares at spot 2 are the Linux picture: game code
  21.7 %, NiMain 14.4 %, Havok 11.1 %, CRT 10.3 %, ntdll 31 % (the waits);
  in the pass renderer/BS 23 %, D3D9.DLL 23 %, OBNG.dll 12 %, OBVR.dll 7 %.
- **The heavy stretch of 2026-11-07 (GPU 12-15 ms) is not the Gold Road**:
  that capture was live play at an unknown place with iMultiSample=4; the
  evening captures of 2026-11-08 (1-32, 1-33, live, after OBVR 7d8b9397)
  held 90 Hz with GPU means of 1.9 and 2.7 ms. Which place and what
  setting made the 7 November frames drop is still open; the Gold Road
  scenario does not reproduce it.
- Spot 3's capture (1-38) sampled sparsely (1 032 samples in the 10 s
  window against 8 143 at spot 2) and 86 % of its out-of-pass samples are
  the wait: the lightest spot.

## NiAVObject::Update by caller (2026-11-09 14:23, [Probe] SceneUpdate with the site table, run obng-profile-gold-road-single/20261109-142316)

OBNG.dll db25d956 (master e994908: the scene update probe keeps calls and
ticks per return site of NiAVObject::Update 0x00707370, 160 slots for the
exe's 145 call sites), the Gold Road single-pass profile scenario, the
route on, OBVR.dll 1391cd67, OblivionWalkers on, Linux headless, nothing
else running on the machine. The first run (-141833, 32 slots) is void: the
slots were claimed while the cell loaded and 67 of the 70 play-time calls a
frame landed in others. Per grid update (one a frame), the 10 s windows
(measured; the spots assigned from the script's timeline, derived):

| windows | spot | NiAVObject::Update calls / ms a frame | 0x00471FBE (ApplyAnimData) calls / ms | 0x78D11025 (OBVR UpdateNodeTransforms) calls / ms | everything else |
|---|---|---:|---:|---:|---:|
| 3-6 | 1 | 62-65 / 0.65-0.68 | 24-25 / 0.50-0.54 | 22-26 / 0.11-0.14 | 13-15 calls, 0.03 ms |
| 7-11 | 2 and its walk | 74-90 / 0.95-1.01 | 33-35 / 0.78-0.83 | 22-25 / 0.11-0.13 | 16-32 calls, 0.03-0.05 ms |
| 12-13 | 3 | 78-117 / 0.83-0.95 | 17 / 0.65-0.67 | 22-39 / 0.11-0.20 | 38-61 calls, 0.03-0.08 ms |

- **One call site takes four fifths of the time**: 0x00471FBE, the call
  inside ActorAnimData::ApplyAnimData 0x00471F20 (`kApplyAnimData`, OBVR's
  GameAddresses.h; Enhanced Camera's name). Read (re/export 0x471f20.c):
  the function takes the AnimData, detaches the controller of the two
  objects at +0x04 and +0x08 (0x00478300 swaps the refcounted pointer at
  +0x0C), calls NiAVObject::Update on the node at +0x94 with the flag 1,
  and attaches the two controllers again - the skeleton's downward pass
  with the controllers out of the way, one per actor. Its callers are the
  actor update 0x00603CA0 (unconditional in its path after the process
  level tests, `in_ECX[0x2c]`) and 0x00474510 (0x00471C00 then
  ApplyAnimData). 25 calls a frame at spot 1, 35 at spot 2, 17 at spot 3:
  one per actor the update runs for (derived from the counts; which actors
  is unread), 20-23 us each at spots 1-2 and 38 us at spot 3 (fewer, nearer
  or busier actors; unread).
- **The second site is OBVR's**: 0x78D11025 is the one call in OBVR's
  `UpdateNodeTransforms` (GameCamera.cpp:205; every OBVR caller - hands,
  weapon, player body, menu nodes, teleport - funnels through it). 22-26
  calls a frame at 5 us each, 0.11-0.14 ms: small nodes. An OBVR item, not
  an engine one; noted for OBVR.
- **The other 143 sites together are 0.03-0.08 ms a frame**: the player's
  own update sites (0x0058FD7D 2 calls, 0x0047CADA 4 calls, 0x0049C335,
  0x0049C88D, 0x0040C9AA, 0x0040C9F5, 0x0066BE91 the camera node, 0x004425BC
  one call each), the cell loader's sites while a cell streams in (window
  7 and 11: 0x004DC20C 7-8 calls). Nothing to gain there.
- **What an actor update LOD can gain (derived)**: ApplyAnimData is
  0.5-0.8 ms a frame here, 20-23 us an actor. Running it every second
  frame for the actors beyond some distance saves half their share: with
  half the actors distant, 0.13-0.2 ms a frame on the Gold Road, more in a
  city or a battle where the count is higher. The actors' bones then hold
  the previous frame's world transforms on the skipped frame (the
  animation itself keeps running; only the propagation to world space
  lags a frame). OBVR's body IK and hit tests read the player's and the
  near actors' bones, which an LOD leaves at full rate. Medium (about
  0.8-1.2 M tokens): the distance must be read at the call site (the
  actor's position against the player's), the skip must leave the
  shadows' and the culler's bounds consistent (unread), and the headset
  decides whether 37 Hz propagation on distant actors is visible.
- The probe's own cost sits inside the numbers (the detours on the
  tree's functions, see the caveat above); the ratio between the sites is
  what this run establishes.

## The actors' update against their number (2026-11-09 14:33, run obng-actors-count/20261109-143325)

tools/hand-scripts/obng-actors-count.txt: the Gold Road's spot 2, the
player still, then the opening's five cast NPCs (00023F2A 00023F2B 0002349F
00023F2E 00025200, armoured Blades and the Emperor) placed at the player's
feet with `player.placeatme`, 20 at t=46 s, 20 more at t=66 s, 40 more at
t=86 s, their AI on. OBNG.dll db25d956, OBVR.dll 1391cd67, Walkers on,
Linux headless at 75 Hz vsync. Measured:

| step | actors updated (ApplyAnimData calls a frame) | ApplyAnimData ms a frame | frames in 10 s | frame ms mean / p95 (perf-phases) | OBVR frame_end_callback mean |
|---|---:|---:|---:|---:|---:|
| spot 2 as loaded (the Walkers' road this time) | 61 | 1.22-1.30 (20-21 us each) | 700-750 (vsync) | 13.35 / 14.3 | 0.9 ms |
| +20 NPCs | 85-86 | 2.59-2.83 (30-33 us each) | 638 and 616 (62-64 Hz) | 15.96 / 27.1 (13.7 % over 20 ms) | 1.5 ms |
| +20 more | 105 | 9.8-25 | 168, 43 | 83 / 265 | 16.1 ms |
| +40 more | 109-124 | 34-64 | 15, 12 | 659 / 902 | - (crash) |

- **The clean step is the first**: 24 more actors in the update (20
  placed, the rest the Walkers' arrivals) cost 1.4-1.5 ms a frame in
  ApplyAnimData alone, about 60 us per added NPC against 20-21 us for the
  average actor already there (derived: the armoured cast carries more
  nodes than the Walkers' and the road's creatures, or the cache no longer
  holds the skeletons; which of the two is unread). The frame left the
  vsync: 62-64 Hz steady, 2.5-3 ms more a frame in all, of which
  ApplyAnimData 1.45 ms, OBVR's frame-end callback 0.6 ms, the rest
  (Havok, AI, the controllers, the draws) 0.5-1 ms (derived from the
  frame counts and the phases).
- **The second and third steps are void for the engine's cost**: the
  frame-end callback grew to 16 ms mean / 73 ms p95 and the game ended at
  t=96 s with a std::bad_alloc in OBVR's FitBodyCapsules (OBVR's analysis,
  their docs/mesh-fitted-capsules-spec.md 618d1ac, open bug). The
  ApplyAnimData times of those steps (90-500 us a call) are measured under
  that load and say nothing about the engine; a repeat runs with [Hands]
  FittedBodyCapsules=0 and the NPCs placed apart rather than in one heap
  (`placeatme id 1 distance direction`), 20/40/60.
- **What this does to the levers (derived)**: a street with 20-25 armoured
  NPCs in the update costs 1.2-1.5 ms a frame in the skeleton propagation
  alone, three times the Gold Road's whole ApplyAnimData share. Two
  levers, both scaling with the actor count: (a) the actor update LOD -
  distant actors propagated every second frame, 0.5-0.8 ms in such a
  street, medium (0.8-1.2 M), changes behaviour at distance; (b) a rebuild
  of the propagation itself (NiNode::UpdateDownwardPass 0x0070A0D0 with
  NiAVObject::UpdateWorldData 0x0070C120 and the bound merge) as OBNG's
  tight loop - the same transforms in the same order, so bit for bit the
  same world matrices, gated by a shadow run - medium to large
  (1.5-2 M), no behaviour change, its gain the loop's share of the 20-60
  us per actor (unread: a probe of the nodes per actor and the time per
  node decides it; cheap, 0.1 M).

## The actors' update against their number, the clean run (2026-11-09 14:51, run obng-actors-count/20261109-145136)

The same scenario with the NPCs placed apart (two a line at 150-600 units in
the four directions) and OBVR's fitted body capsules off ([Hands]
FittedBodyCapsules=0); OBNG.dll c87a5fb6 (master 8e97433: the probe also
bins each ApplyAnimData call by the nodes it visits), OBVR.dll 1391cd67,
Linux headless at 75 Hz vsync, PASS, no crash at 124 actors. Measured:

| step | actors updated a frame | ApplyAnimData ms a frame | engine frames in 10 s | frame ms mean / median / p95 (perf-phases) |
|---|---:|---:|---:|---:|
| spot 2 as loaded | 64 | 1.31-1.36 | 745-750 (vsync) | 13.36 / 13.34 / 14.2 |
| +20 NPCs | 83-87 | 2.30-2.47 | 732, 722 | 13.71 / 13.33 / 14.5 |
| +20 more | 104-106 | 3.28-3.43 | 645, 651 | 15.25 / 13.84 / 26.5 |
| +20 more | 123-124 | 4.11-4.30 | 579, 576, 523 | 17.56 / 16.77 / 23.2 |

- **Linear in the count**: each 20 armoured NPCs add 1.0 ms a frame in
  ApplyAnimData, 50 us a NPC, from 64 to 124 actors without a knee; the
  frame leaves the vsync at about 85 actors and is at 55-57 Hz with 124
  (the Walkers' arrivals account for the 3-7 actors above the placed
  count). OBVR's frame-end callback stays at 1.8 ms throughout, so the
  run of 14:33 collapsed on the capsules alone.
- **Three kinds of actor, by their nodes** (the probe's buckets, every
  window the same within a few percent):

  | nodes a call (NiNode::UpdateDownwardPass visits) | who (derived) | calls a frame | us a call | ns a node |
  |---:|---|---:|---:|---:|
  | 27 | one actor: the player's own skeleton (one call a frame) | 1 | 9-11 | 340-400 |
  | 78 | the road's actors: the Walkers' NPCs and the creatures (plain clothes) | 59-60 | 19-21 | 245-270 |
  | 303-325 | the placed cast (Blades armour: cuirass, greaves, gauntlets, boots, helmet, sword, shield, each a subtree) | 20 / 40 / 60 | 46-57 | 152-175 |

  UpdateWorldData runs once per node visited plus a few (94 for 78 nodes,
  325-349 for 303-325): the propagation is the whole call.
- **A node costs 150-400 ns, not 50** (the grid update's estimate above
  was the count's lower bound): the compose of parent and local
  transforms is 20-40 ns of it; the rest is the walk - the virtual
  UpdateDownwardPass per child, UpdateObjectControllers' list checks,
  the world bound update, the pointer chasing through nodes allocated
  over the game's life. The per-node cost falls as the skeleton grows
  (400 -> 250 -> 160 ns) as the fixed per-call part amortises.
- **The decision between the two levers (derived)**: an armoured NPC's
  cost is its 300 nodes, four times a plain NPC's; a street's cost is
  the sum. The LOD halves the count for the distant half; a rebuild of
  the propagation attacks the 160-270 ns a node, where the same
  transforms in the same order in one flat loop over a per-skeleton node
  array (built once per skeleton, invalidated when the tree changes) is
  the shape that keeps the results bit for bit and drops the walk. The
  two are independent and add up; the rebuild is the one that costs
  nothing in behaviour. The probe's fixed cost per counted node (an
  interlocked increment, 10-20 ns) sits inside these numbers.

## One actor's update split into its steps (2026-11-09 15:17, run obng-actors-count/20261109-151745)

OBNG.dll 5cbd3b6e (master a29afc1): inside each ApplyAnimData call the
probe times UpdateWorldData 0x0070C120, UpdateObjectControllers 0x0047C930,
NiBound::Merge 0x0072A6B0 and NiGeometry::UpdateWorldBound 0x00722AA0 on
the render thread; the call's time minus the steps' is the walk. The
probe's own cost per timed step is 41 TSC ticks (two TSC reads, median of
1000 at install; about 12 ns at this machine's TSC rate, derived) inside
each step's time, plus the detour's jump, trampoline and flag test (about
5-10 ns, derived) in the walk. OBVR.dll 7a81030d, [Hands]
FittedBodyCapsules=0, the same scenario as 14:51. Per call, measured (the
windows agree within a few percent; the probe's share in brackets,
derived):

| actor | nodes | call | UpdateWorldData | controller walks | NiBound::Merge | NiGeometry::UpdateWorldBound | walk |
|---|---:|---:|---:|---:|---:|---:|---:|
| player (1 a frame) | 27 | 10.6-11.9 us (of it probe about 1.5) | 28 calls 4.9-5.5 us | 28 calls 1.7-2.0 us | 55 calls 1.3-1.4 us | 1 call 1.4-1.5 us | 1.3-1.8 us |
| plain NPC or creature (the road's 50-60) | 78 | 22.1-23.7 us (probe about 4) | 93 calls 8.5-9.4 us | 93 calls 1.4-1.8 us | 127 calls 3.5-3.7 us | 15 calls 6.2-6.6 us | 2.0-2.2 us |
| armoured NPC (the placed cast) | 303-325 | 51.7-59.5 us (probe about 12-20) | 325-349 calls 14.4-18.4 us | 319-343 calls 9.6-19.9 us | 254-331 calls 6.0-6.4 us | 22-24 calls 6.3-8.0 us | 10.9-13.0 us |

- **UpdateWorldData is the largest step but not the whole**: 40 % of a
  plain NPC's call, 30 % of an armoured one's; about 50 ns a node after
  the probe's 12 (derived): the parent-times-local compose with its
  scale and rotation cases (re/export 0x70c120.c: a 0x34-byte frame, the
  branches on the parent's presence), not 20.
- **The controller check costs what it finds nothing in**: 1.4-2.0 us for
  93 nodes of a plain NPC (15-20 ns a node: the two pointer tests of
  0x0047C930), but 9.6-19.9 us for 320-340 nodes of an armoured one
  (30-60 ns a node), falling from 19.9 to 9.6 us over the run as the
  placed NPCs settled (their idle and look controllers quiet down;
  derived). The skeleton's own controllers are detached for the call
  (ApplyAnimData's two SetController(0)); what remains is the armour
  parts' and the weapons' property and controller lists.
- **NiGeometry::UpdateWorldBound is the expensive step per call**: 400 ns
  each for a plain NPC's 15 geometries and 300 ns for the armoured's 22 -
  the skinned bound through 0x0072BB30 (per bone NiBound::Update under the
  bone's world transform, then merges). 28 % of a plain NPC's call: as
  large as its share of the controller checks, merges and walk together.
- **NiBound::Merge is cheap now**: 127 calls in 3.5 us, 27 ns each with
  the probe inside (the square root route on; before it, every merge paid
  the CRT's INEXACT path).
- **The walk** (recursion, the two virtual calls a node, the bound copy
  into the parent, the probe's detour overhead): 9 % of a plain NPC's
  call, 20 % of an armoured one's; after the probe's share (about 970
  detour passes at 5-10 ns for the armoured: 5-10 us) perhaps 3-6 us of
  the armoured call, about 1 us of the plain one.
- **What a rebuild of the propagation gets (derived)**: every step is a
  per-node function reached through a vtable or a list; a flat per-skeleton
  array (node, parent index, local transform pointer, flags: has
  controllers, is geometry, is skinned) built once per skeleton and
  invalidated when the tree changes lets one loop do the compose (the
  same operations in the same order: bit for bit), skip the controller
  check where the flag says there is none, merge the bounds in a second
  pass, and run the skinned bounds as one vectorisable loop over the
  bones. Against the measured split: the controller checks and the walk
  go nearly whole (30-40 % of an armoured call, 15 % of a plain one), the
  compose and the skinned bound keep their arithmetic and lose their call
  overhead (a quarter to a third of theirs, guessed). About 40-50 % of
  the call, so 0.5-0.7 ms a frame on the Gold Road and 2 ms a frame in
  the 120-actor scene (derived). Medium to large (1.5-2 M): the array's
  invalidation (who edits a skeleton's tree: OBVR's hands and dismember,
  the engine's equip and ragdoll) is the risk, the shadow gate the
  acceptance.
- **Caveat, unexplained**: this run's frame left the vsync far harder
  than 14:51's with the same actor counts and the same per-actor time
  (frame 20.7 / 24.3 / 29.7 ms mean at the three steps against 13.7 /
  15.3 / 17.6; 454-503 frames in 10 s at 75 actors against 722-732);
  OBVR's frame-end callback was small (0.6-1.0 ms). Two things changed:
  OBVR.dll 7a81030d (1391cd67 before) and the probe's two new detours on
  NiBound::Merge and NiGeometry::UpdateWorldBound, which every merge and
  bound update of the frame pass through, in and out of the actor calls.
  A run with the same DLL and [Probe] SceneUpdate=0 separates the two
  (cheap, one harness run); until then the frame times of this run are
  not evidence about the engine, only the per-actor split is.

### The frame drop resolved: the slow runs drew the NPCs, the fast one did not (2026-11-09 15:32, control run obng-actors-count/20261109-153251)

The control run with the same OBNG.dll 5cbd3b6e and [Probe] SceneUpdate=0
(no scene update detours at all) was as slow as 15:17: frame 13.4 / 22.9 /
25.8 / 34.1 ms mean at the four steps, scene_pass 4.0 / 7.7 / 9.8 / 12.4 ms,
the GPU before submit 7.1 / 10.5 / 12.5 / 15.5 ms, OBVR's frame-end
callback 0.8-0.9 ms. So the probe is acquitted, and so is OBVR's callback.
OBNG's own draw counters per 10 s window (the Route's stereo draw summary)
tell the difference between the runs (measured):

| run | OBVR.dll | whole draws a frame before / after the spawns | skinned draws a frame before / after |
|---|---|---:|---:|
| 14:51 (-145136), frame 13.7-17.6 ms | 1391cd67 | 360 / 95-110 | 9 / 15-20 |
| 15:17 (-151745) and 15:32 (-153251), frame 20.7-34.1 ms | 7a81030d | 370 / 560-600 | 9 / 125-320 |

In the fast run the placed NPCs were never drawn and two thirds of the
world's draws went away with them: the view or the camera must have
changed with the spawn (which way is unread; the headless screenshots are
black). The slow runs drew the 20 / 40 / 60 armoured NPCs: 230 more
skinned draws a frame (each a bone palette and two eyes) and 200 more
whole draws, 8 ms more scene pass on the CPU and 8 ms more GPU at the
Beyond's resolution under DXVK. The frame times of 14:51 are therefore no
measure of a crowd's cost; the ApplyAnimData numbers of all runs stand,
since the skeleton update runs whether the actor is drawn or not.

What this says about a crowd (derived, Linux/DXVK): 60 armoured NPCs in
view cost the frame about 20 ms - 3 ms of skeleton update, 8 ms of draw
submission, 8 ms of GPU - and the draw path, not the update, is the larger
CPU part once the actors are in view. The update's rebuild (above) is
worth its 40-50 % of 3-4 ms in such a scene; the draws want their own
lever (fewer draws per armoured NPC: the armour parts share a skeleton and
a shader and could be one skinned draw per material, or the pass could
instance them - unread, OBVR's and OBNG's render path together) and the
GPU side is OBVR's resolution and MSAA. On Windows with native D3D9 at 90 Hz
the draw submission's CPU cost differs and must be measured there.

## Lever built: the actors' skeleton update as a flat run (2026-11-09 16:30-17:13)

The rebuild (src/engine/scene/SceneUpdateLogic.h, SceneUpdate.h; plugin/src/
FastSceneUpdate.cpp; docs/subsystems/scene-update.md; re/functions/
0x0070A0D0.md): NiAVObject::Update's body for the call from
ActorAnimData::ApplyAnimData, the subtree's controllers, world transforms
(0x0053D7A0's x87 order, bit for bit) and bounds from a flat per-skeleton
order instead of the recursion. Runs on the actor-count scenario (Gold Road
spot 2, 20/40/60 placed NPCs), Linux headless:

- **Gate** ([Shadow] ActorUpdate, run -165802): 374 191 calls, 0 mismatches
  - every world transform against the original compose, every node's bound
  against the original NiNode::UpdateWorldBound, in the game. Two earlier
  gate designs and one reading error fell on the way: a second run of the
  subtree in a frame is not the same frame (the heads step the face
  animation by the tick, controllers fire once), so the gate verifies each
  value where it is made; and FCOM's unordered result (C3, C2 and C0: even
  parity) means a NaN child radius is merged, not skipped, and a NaN node
  radius merges rather than copies (744 calls on one actor showed it, run
  -165345; the oracle's leaves carry NaN radii since).
- **Measured, the same clock around the call** (QueryPerformanceCounter;
  [Probe] ActorUpdateTiming for the original, run -170740; [Route]
  ActorUpdate, run -170054; the same scenario, both runs drawing the
  scene):

  | actors in the update | original, us a call | flat run, us a call | saved |
  |---:|---:|---:|---:|
  | 64 (the road's: plain NPCs, creatures, the player) | 21.0-21.5 | 13.3-13.9 | 36 % |
  | 85 (+20 armoured) | 30.1-31.1 | 18.5-18.8 | 39 % |
  | 105 (+40) | 33.6-33.8 | 21.3-21.9 | 36 % |
  | 124 (+60) | 33.2-36.0 | 22.2-23.9 | 34 % |

  Per frame (derived): 1.36 -> 0.87 ms on the Gold Road (0.5 ms saved),
  4.2 -> 2.8 ms with 124 actors (1.4 ms saved). The frame time cannot show
  it in this scenario: at 75 Hz vsync on the road, and GPU- and draw-bound
  with the crowd (scene pass 7-14 ms, GPU 10-17 ms, run-to-run draw
  variance larger than the saving). The 90 Hz Windows run at a CPU-bound
  spot is where it would show.
- **What the route does with a changing tree**: 0-10 orders a window are
  rebuilt after a shape change (equip, the heads' attachments); no call fell
  back to the original (every root a plain NiNode).
- **Scenario caveat**: in two of six runs (14:51, 17:03) the scene stopped
  being drawn after a spawn step (scene pass 0.5 ms, GPU 4 ms) - the placed
  NPCs around the player likely displace the view (unread); those runs'
  frame times are void, the per-call times are not.
- Off in the shipped OBNG.ini ([Route] ActorUpdate=0) until Nadi decides,
  as the square root was.

## The grid update split (2026-11-09 18:31, run obng-profile-gold-road-single/20261109-183144)

[Probe] SceneUpdate with the grid split (OBNG.dll 176897c7): inside
GridCellArray::Update 0x004823D0 the top-level selected downward passes
0x0070A190 and upward passes 0x00709EC0 timed, the steps counted. Read
first (re/export 0x4823d0.c, 0x4d4970.c): the grid update runs
TESObjectCELL::UpdateSelectedNodes 0x004D4970 per cell (the interior and
the checkerboard half of the border), which runs the selected update
0x007073A0 on up to three grandchildren of the cell node (children 2, 3, 4
of the cell node, each one's child 3), skipping a hidden one (flags bit 0);
0x007073A0 is the node's +0x64 (the selected downward pass) and then the
parent's +0x94 (the upward pass). The selected pass visits only the
children flagged with bit 1, runs the controllers for bit 3 and the world
data for bit 2, and merges every child's bound (visited or not). Per grid
update, the 10 s windows at the three spots (measured, the probe's own
cost inside):

| grid update | selected passes | visited nodes | world data | controller walks | merges | upward passes | the rest |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1.10-1.26 ms (spot 1) | 68, 1.03-1.19 ms | 1320-1640 | 2600-3200 | 2600-3200 | 3960-4170 | 68, 0.06 ms | 0.01 ms |
| 1.47-1.87 ms (spot 2) | 68, 1.17-1.52 ms | 1090-1440 | 2170-2850 | 2160-2840 | 4210-4430 | 68, 0.15-0.28 ms | 0.04-0.13 ms |
| 1.14-1.54 ms (spot 3) | 68, 0.89-1.17 ms | 700-900 | 1400-1800 | 1400-1770 | 3860-3940 | 68, 0.23-0.28 ms | 0.03-0.10 ms |

- **The grid update is the selected pass**: 68 passes a frame (17 cells,
  four roots each), 0.9-1.5 ms, the upward passes 0.06-0.28 ms, nothing
  else. About 400 ns per world data (two per visited node: the node and
  its flagged leaves), 2-3 merges per visited node: the statics of 17 cells
  are cold memory, far slower per object than an actor's skeleton (150-400
  ns a node, warm).
- **The earlier attribution was wrong**: the 5 000-9 000 node visits a frame
  counted on 2026-11-08 were the actors' full passes, not the grid's; the
  grid visits 700-1 600 nodes a frame (corrected here).
- **What a flat run would get (derived)**: the walk's share - the virtual
  call per child, the recursion, the per-child flag and bound reads done
  through the port - about a quarter to a third, 0.3-0.5 ms a frame; the
  cold first touches stay. The order must follow the flags: which children
  are visited changes with the controllers, so a flat order per root is a
  list of candidates validated against the flags each frame, and the
  merges run over every child as the original's do. Medium (0.6-1 M), the
  same gate pattern as the actors' update, with the rigid variant (+0x68,
  bit 4) to read first.
