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
