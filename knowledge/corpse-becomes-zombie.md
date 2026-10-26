# A corpse that becomes a zombie

Status: knowledge, written 2026-10-25 from the OblivionWalkers mod
(`../OblivionWalkers`, docs/walkers-spec.md 4.1) and OBVR. Nothing of this is
built in OBNG.

## The task

A person dies; some seconds later the body gets up as a zombie - another
model, other AI, the person's belongings on it - from the very spot and pose
the body lies in.

## How Oblivion forces it to be done (what the mod does)

Oblivion cannot turn a reference into another kind of actor. A dead person
is a `Character` reference (vtable 0x00A6FC9C) on a `TESNPC` base; a zombie is
a `Creature` reference (0x00A710F4) on a `TESCreature` base. Swapping the
base record of the reference to a creature record is not an option: the
reference's class, its process and its 3D are built for the kind it was
placed as. So the mod uses a body double, step by step, each from a frame
tick (xOBSE task queue):

| Step | Engine call (1.2.416) | Why |
|---|---|---|
| Place the zombie on the corpse | console `PlaceAtMe <walker> 1 0 0` on the corpse, then find the new reference by identity (the zombie references of that base before vs. after) | the engine puts a placed creature where it likes outdoors (670 units off on a slope, measured), so no radius search |
| Wait for its model | its `niNode` (ref +0x3C) non-null | a kill before the 3D exists leaves a "dead" actor sliding at walking speed (400 units/s, measured) |
| Make it unseen | console `SetActorAlpha 0` | its fall must not be seen |
| Put it onto the corpse, kill it | `MoveTo <corpse>`, `Kill` | the ragdoll comes on with the kill |
| Let the ragdoll settle | ~1.5 s | |
| Lay it into the corpse's pose | per ragdoll bone: corpse bone's world transform into the zombie's body (below) | the zombie must lie where and how the corpse lies |
| Swap visibility, move the items | corpse `Disable`; zombie `SetActorAlpha 1`; corpse `RemoveAllItems <zombie>` | one frame after the pose, so the nodes have followed the bodies |
| Raise it | `Actor` virtual +0x20C (the console's ResurrectActor) with (1, has3D, animate=1) | plays the engine's get-up animation from the ragdoll |

### The pose copy, as read in the EXE

- Bone node by name: `NiAVObject::GetObjectByName`, vtable +0x58,
  thiscall(name).
- A node's collision object: cdecl 0x0047FAC0(node); the body wrapper
  (`bhkRigidBody`) at +0x10; the Havok body (`hkRigidBody`) at wrapper +0x08;
  its `hkMotion` at +0x50 (rotation columns +0x10, translation +0x40, linear
  velocity +0xD0, angular +0xE0, mass inverse +0xC0).
- Teleport a body: the wrapper's `SetTranslationAndRotation`, vtable +0xA0
  (0x008A2FB0 for `bhkRigidBody`), thiscall(const hkVector4* pos, const
  hkQuaternion* rot), 16-byte aligned, position in Havok units (game units x
  0.142877, the scale at 0x00A39088), rotation (x, y, z, w). The engine's own
  node-to-Havok push 0x0089EAE0 calls it so: the node's world transform,
  rotation through `NiQuaternion::FromRotation` 0x007150F0, inside the Havok
  lock (object 0x00BA7B00, enter 0x0043F2E0, leave 0x0043F300).
- Still the body: `hkMotion` vtable +0x54 setLinearVelocity, +0x58
  setAngularVelocity; wake it: 0x008A6410 thiscall(hkRigidBody*).
- A node's world transform: rotation +0x64 (m[row][column], world = R *
  local), position +0x88, scale +0x94.

Measured (harness, 2026-10-25): 13 to 16 bodies on a zombie's ragdoll (the
spine three, head, arms, hands, legs, feet; one zombie model carries no left
arm), 108 to 496 microseconds for the whole copy. No body hangs on
`Bip01 Pelvis` on these skeletons; the pelvis node and the reference's
position follow the moved bodies only frames later (measured: still the old
spot one frame after the move, the new one three seconds on).

What held and what did not (harness 2026-10-25, the log's pelvis and head of
both bodies before, one frame after and three seconds after):

| Way | Right after | Three seconds on |
|---|---|---|
| Whole ragdoll moved and turned about the vertical (pelvis onto the corpse's pelvis, pelvis-to-head heading matched) | head on the corpse's head | 4 degrees off the corpse's heading, pelvis 8 units off - **holds** (measured) |
| Each body onto the human bone's world transform | head on the corpse's head | 55 degrees off, head 32 units off - **does not hold** (measured) |
| Retargeted: the human bones' world turns along the zombie's own bone lengths, down from the pelvis | head 1 unit off | 46 and 83 degrees off in two runs - **does not hold** (measured) |

Derived at the time (superseded below - the cause was a body left behind
and the corpse's collision): moving the zombie's own, self-consistent pose
holds; anything that puts the human skeleton's turns onto the zombie's
bones is pulled away by the zombie's joints. Suspected, not proven: the two skeletons' bone axes
differ, so a human bone's world turn twists a zombie bone against its joint
(measured 2026-10-25 12:50: the axes agree - the child bone lies along the
bone's own x on both skeletons, 1.00 0.00 0.00, for spine, neck, arm and
leg bones; only pelvis to spine differs by some 20 degrees; the roll about
x is not covered). So the axes are not the cause either; the suspects left
(unverified) are the zombie's joint limits and its limbs meeting the floor
at other lengths. If they differ, the transfer has to go by
bone *directions* (axis-free) or through both skeletons' bind poses - which
is what a modern retargeter's "retarget pose" step is for (below).

Measured 2026-10-25 (harness, the log's spine and head positions): the
corpse's ragdoll is a physical body to the zombie's. Laid onto the same spot,
the two push each other - the corpse's head moved 50 units and rose 20, the
zombie ended sitting on it. With the corpse disabled in the frame the zombie
was laid, the zombie's placement held to 1 unit. Two actors' dead ragdolls
collide in Oblivion; a body double needs the original's collision out of
the way before it is laid into its place.

### The collision filter, as read in the EXE (REA, 2026-10-25)

- A body's filter word: `hkRigidBody` +0x30 (the collidable at +0x14, its
  filter info at +0x1C). Low 6 bits the layer, bits 8 to 12 the part
  number, 0x4000 "no collision", 0x8000 "linked", the high 16 bits the
  system group (**read**; layout also in OBVR `src/game/GrabPhysics.h`).
- Groups: 0x00889C20 gives every body of one object the same group - a
  per-layer default from 0x00BA7EB0, else a fresh one from the counter
  0x00B2EB3C (wraps to 10) (**read**).
- The rule, 0x008A7F70 cdecl(a, b), returns whether two bodies may collide
  (**read**, every step):
  1. layer of a is not 29 and either has 0x4000 - no;
  2. either group 0 - yes;
  3. both layer 8 (biped): two groups - yes; one group - the bone table
     `0x00BA7E30[part a]` bit `part b`;
  4. otherwise: two groups - the layer matrix `0x00BA7DB0[layer a]` bit
     `layer b`; one group and both linked - the matrix, and parts not
     next to each other (|part a - part b| != 1); one group, not both
     linked - no.
  The matrix (32 rows) and the bone table (32 rows by part) lie back to
  back, so a layer above 31 reads into the bone table.
- 0x008A83C0 fills them: matrix all ones, then rows cut back (biped meets
  neither NONCOLLIDABLE, PORTAL, CHARCONTROLLER, AVOIDBOX nor PATHPICK);
  bone table zero, then rows set - no bone row has bit 0 (**read**). The
  matrix setter 0x008A7F20 cdecl(layerA, layerB, bool) is symmetric.
- Writing a filter: the engine's setters 0x0089F4D0 (group) and 0x0089F520
  (the 0x4000 switch) write the word, then call the wrapper's vtable +0x80
  (0x008B0060), which calls `hkWorld::updateCollisionFilterOnEntity`
  (0x0089B630, full check) when the body is in a world (**read**).
- Measured (harness 2026-10-25 15:27): a human corpse's and a zombie's
  ragdoll bodies are all layer 8, parts 1 to 16, not linked, one group per
  actor (0xCD and 0xCF) - so by step 3 every pair of their bodies collides.
- Derived (`OblivionWalkers/src/ApartLogic.h`): the corpse's bodies moved
  into the zombie's group with part 0 collide with none of the zombie's
  (step 3, bone row 0 empty and no row with bit 0) nor with each other,
  while every pair with anyone else is decided as before (the layer is
  kept). Built as `[Rise] KeepCorpseApart`.
- Measured (harness 2026-10-25 18:04 to 18:09, four runs): the move takes
  every time (the corpse then reads 00CF0008 beside the zombie's
  00CF0208), no body falls through the floor, the corpse lying on is no
  longer pushed (0 units in the one-frame runs), and the zombie laid into
  the corpse's pose and settled ends 4 units off the corpse's spot in both
  runs - where with both colliding it ended sitting on the corpse. The
  whole-body placement alone still turned 80 degrees in one run of two,
  with nothing to push it: a second cause, not the filter (spec 4.1).
- The second cause, measured 2026-10-25 18:29: a skeleton carries a body
  on "Bip01 NonAccum" (a biped, part 2 - the hips' body on skeletons whose
  "Bip01 Pelvis" carries none) and a human corpse one on "SideWeapon"
  (switched off, 0x4000). A list of "Bip01 ..." bone names misses both:
  the zombie's hip body stayed where it fell and pulled the moved ragdoll
  round; the corpse's two stayed in its own group and still met the
  zombie. Taking every body by walking the node tree (children at NiNode
  +0xB0, count +0xB6; the class from MSVC RTTI ending in "Node") instead
  of by name: five runs, the zombie 0 to 1 unit and 0 to 2 degrees off the
  corpse, the copied pose drifting 2 and 5 degrees (46 to 83 before), the
  corpse not moved at all. **Lesson for any engine:** enumerate a
  ragdoll's bodies from the physics side (the ragdoll instance's body
  list), never from a list of bone names.

### The double's death is heard (read 2026-10-26)

- The engine's death, 0x006005F0 thiscall(actor, killer), plays the death
  sound inside the call: a creature's TESCreature::GetSound(8) (0x0051CEC0
  thiscall(creature, type), ret 4: own sound lists at +0x100 when the flags
  at +0x28 carry 0x100, else the sound base at +0x100 is followed; ten list
  heads, one per type; 0x00519900 takes the first sound whose chance the
  dice pass), made at the actor with 0x006AE0A0 (flags 0x102), placed,
  attached and started; a person speaks its Death topic instead. Only with
  a high process and within a distance cut (**read**).
- So a body double killed unseen is still heard - alpha hides the mesh, not
  the voice. The mod empties the death list for that one kill and plays the
  drawn sound when the double gets up (**built, not yet heard**).
- A modern engine: an entity's audio emitter belongs to the entity's
  presentation state - muted, or never created, while the entity is hidden
  or not yet "spawned in" - instead of every gameplay event playing its
  sound unconditionally (derived).

### What hurts in this design

- Two actors for one: the corpse and the zombie exist side by side for
  seconds; every step can fail separately (no model, a mod disabling the
  corpse, a save in the middle) and needs its own fallback.
- No shared skeleton contract: the corpse's and the zombie's skeletons are
  matched by bone *name* only; proportions differ, so the copied pose is
  pulled by the zombie's joints after it is laid.
- Visibility by alpha is a trick, not a state: an interrupted swap can leave
  an invisible actor.
- Everything goes through console lines (script compile per call) because
  the engine exposes no API for it.

## How a modern engine would do it

The core difference: **the entity stays, its parts change.** There is no
second actor and no hiding.

1. **Same entity, new components.** In an entity-component engine (or a
   UE `ACharacter`), "dead person" and "zombie" are states of one entity:
   swap the mesh and the material, the AI behaviour, the faction, the stats -
   the transform, the inventory and the ragdoll stay where they are. In
   Unreal, swapping the skeletal mesh on a component that shares the
   skeleton keeps the bone pose; with a different skeleton the pose is
   carried over by *retargeting* (IK Retargeter, the `Retarget Pose From
   Mesh` node) or, for matching bone names, `Copy Pose From Mesh` ("will
   only copy matching bones, and everything else will use the reference
   pose" -
   https://dev.epicgames.com/documentation/unreal-engine/modular-characters-in-unreal-engine?application_version=5.1).
   Oblivion's name matching is the poor man's version of that - and the
   measurements above show why a real retargeter matches the two skeletons'
   reference poses first ("Retarget Pose", IK Rig docs:
   https://dev.epicgames.com/documentation/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine)
   instead of copying world rotations bone by bone.
2. **Teleport the physics bodies through the physics API, not memory.**
   - Jolt: `Ragdoll::SetPose(skeletonPose)` calls
     `BodyInterface::SetPositionAndRotation` for each body, by default
     without activating them; `Ragdoll::Activate()` wakes them
     (deepwiki jrouwe/JoltPhysics, Ragdoll and Skeleton Systems).
     `DriveToPoseUsingKinematics` / `DriveToPoseUsingMotors` move a ragdoll
     to a pose over time instead of in one jump.
   - PhysX 5: `PxRigidActor::setGlobalPose` for free bodies; for an
     articulation (a ragdoll as one reduced-coordinate chain)
     `setRootGlobalPose` plus joint positions (`setJointPosition` and
     `updateKinematic`, or an `PxArticulationCache` with `applyCache`);
     velocities zeroed with `setLinearVelocity` / `setAngularVelocity` /
     `setJointVelocity` (deepwiki NVIDIA-Omniverse/PhysX).
   Oblivion's Havok 4 path is the same idea one body at a time: the
   wrapper's `SetTranslationAndRotation`, zero velocities, activate.
3. **Get up from the ragdoll's own pose.** Unreal snapshots the ragdoll's
   final pose (`Save Pose Snapshot` / `Snapshot Pose`), stops simulating and
   blends from that snapshot into a get-up montage chosen by whether the body
   lies face up or face down
   (https://docs.unrealengine.com/4.27/en-US/AnimatingObjects/SkeletalMeshAnimation/PoseSnapshot/).
   Oblivion's `Resurrect` with animate=1 plays its knockdown get-up; whether
   it picks by facing was not read.
4. **Keep the double out of the original's way by filter, not by
   hiding.** Jolt gives each ragdoll a `CollisionGroup` (group ID per
   ragdoll, sub-group ID per part, a `GroupFilterTable` bit table for
   which parts of one group meet); bodies of different group IDs collide,
   and `BodyInterface::SetCollisionGroup` changes a body's group while it
   is in the world (deepwiki jrouwe/JoltPhysics,
   `Jolt/Physics/Collision/GroupFilterTable.h`, `BodyInterface.cpp`).
   PhysX 5 decides pairs in a filter shader over each shape's
   `PxFilterData` (four words); after changing the data on a shape in the
   scene, `PxScene::resetFiltering` re-filters its existing pairs
   (deepwiki NVIDIA-Omniverse/PhysX, `physx/include/PxFiltering.h`).
   Oblivion's Havok rule is the same design - group plus part plus a bit
   table - with `updateCollisionFilterOnEntity` as the re-filter; the mod
   uses it the way Jolt's `SetCollisionGroup` would be used: the original
   joins the double's group for the seconds both lie there.
5. **Powered ragdolls** (motors driving the bodies toward an animation)
   make "lies like a corpse, then struggles up" one continuous motion rather
   than a cut - Jolt's `DriveToPoseUsingMotors` is that.

## What this means for a reverse-engineering pass

- The pieces to find first if Oblivion is ever given "change an actor's
  kind in place": where a reference's 3D and process are built from its
  base (`Update3D` rebuilds the 3D from the base - proven by the mod's clone
  path, which swapped a person's base record and race and saw the new skin
  after `Update3D`), and whether a `Character`'s process can host a creature
  body.
- The Havok calls above are enough to write a `SetRagdollPose(actor,
  pose)` helper - the equivalent of Jolt's `Ragdoll::SetPose` - which every
  later feature (poses from animations, get-up blends, grabbing) can share.
- Not read yet: how the knockdown get-up picks its animation, and whether
  Havok 4's `hkRigidBody` offers a pose call for a whole ragdoll
  (`hkaRagdollInstance`?) that would make the per-bone loop one call.
