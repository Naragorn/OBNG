# Body colliders: what a blade or a hand meets on a person

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`,
docs/mesh-fitted-capsules-spec.md sections 3.2 and 4.7,
`tools/capsule-fit.py`, `tools/nif-skeleton-shapes.py`). Nothing of this is
built in OBNG; in OBVR the fit exists only as an offline prototype.

Marks: **read** (in a file or a source), **measured** (in the game or on
its files), **derived**, **unverified**.

## What Oblivion has

- **Read:** the only per-part shapes on a person are the ragdoll's Havok
  bodies in `skeleton.nif`: one `bhkCapsuleShape` per body part (head,
  spine, spine2, upper arm, forearm, thigh, calf), one skeleton for every
  body and armour. They exist for the ragdoll and the death, not for hits:
  a melee hit names a target, not a point on it (`AttackHandling`
  0x005FEBF0 - OBVR's physical-combat-spec 9).
- **Measured (2026-10-25, offline):** those capsules are thinner than the
  bare body almost everywhere. Radii in game units, vanilla Havok against
  the vertices each bone owns (the core circle, 95 %): head 4.2 / 7.4,
  torso 4.6-5.7 / 12.4, upper arm 2.9 / 5.3, thigh 4.8 / 8.0 on a naked
  man; under steel plate the upper arm is 7.6 and the thigh 11.9, which no
  skeleton capsule follows.
- **Measured:** every body and armour mesh read (14 vanilla meshes, man
  and woman, leather, steel, daedric) is skinned in the skeleton's bind
  pose: a vertex placed through each of its bones (vertex x skin-to-bone x
  the bone's bind transform) lands within 0.077 units. So colliders fitted
  in bone space from the meshes can be placed by the bones alone.

## How a modern engine does it

- **Generated from the mesh, by dominant weight (Unreal).** The Physics
  Asset editor generates bodies per bone from the skeletal mesh: primitive
  type (capsule, sphere, box, convex hulls, tapered capsule for cloth),
  "Vertex Weighting Type" Dominant Weight (each vertex to its strongest
  bone) or Any Weight, "Min Bone Size" (short bones skipped), "Min Weld
  Size" (small bones merged), "Auto Orient to Bone"
  (https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-asset-editor-in-unreal-engine---tools-and-profiles).
  Done once per mesh in the editor, then hand-tuned.
- **Hand-authored simple shapes per part (Blade & Sorcery):** a creature is
  a ragdoll of parts, each with a collider group of simple shapes
  (kospy.github.io/BasSDK, Ragdoll Part, Collider Groups; OBVR's
  weapon-collision-spec 17.1).
- **Derived, for Oblivion:** neither works for a modded game where every
  armour mod brings its own meshes - so OBVR's design fits at run time, per
  worn mesh, cached by mesh (the Unreal generator's rule, run in the game
  instead of the editor).

## What the fit taught (measured on the vanilla meshes)

- Fit groups, not bones: a twist bone owns most of its limb (UpperArmTwist
  105 of 145 vertices), and each spine bone alone owns a thin slab (Spine1
  28) - the torso fits as one group along the spine. This is Unreal's weld.
- Centre the capsule on the flesh, not on the bone: bones sit up to 3.4
  units off their flesh's centre under plate.
- Split only a flat torso: a naked man's chest is 1.41 times wider than
  deep (two capsules side by side save about 3 units of depth), a woman's
  1.23, every cuirass 0.82-0.97 (a barrel); skirts (1.56) and spikes (1.86)
  would trip a threshold applied to every part.

## What this means for a reverse-engineering pass

- An engine-level hit test (a swept blade against per-part shapes, instead
  of reach and cone) needs these shapes per actor; fitting them from the
  worn meshes at load, cached per mesh, is cheap and mod-proof.
- **Measured (OBVR `worn-shapes.txt`, 2026-10-25):** other actors' skinned
  shapes keep their vertices and skin weights on the CPU after the upload
  (97 shapes of five NPCs, a creature among them, all read whole, every
  vertex's weights adding to one), so the fit can read the meshes from
  memory, not from the archive. Actors wearing one mesh share its vertex
  data (two NPCs' `Hand` at one address); each FaceGen face is its own.
  Hair, eyes, mouth, teeth, helmets, weapons and shields are rigid shapes
  under nodes - only body, face and worn armour or clothing are skinned.

## Fitting in the game (OBVR, measured 2026-10-25)

- **Measured:** in the running game the shapes' bind spaces differ: a
  FaceGen face's and the clothes' vertices, put through another skin's
  skin-to-bone, landed at the feet (a head capsule from the floor to the
  head, a forearm 58 units round). Offline the vanilla meshes agree with
  the skeleton's bind; in the game only the bones' world transforms are
  shared. The fit therefore skins each shape with its own skin to the
  current pose and works in the bones' world spaces of that moment.
- **Measured:** a fit of 6000-9300 vertices took 2-3.4 ms on the render
  thread; split in two it costs 0.35-0.58 ms there (reading and skinning,
  the engine's data) and 0.95-1.58 ms on a worker thread (the pure fit).
  An engine with a job system would schedule the second half anywhere; in
  Oblivion one CreateThread per fit, the job abandoned when the actor goes.
- **Measured:** a robe's skirt is skinned to the calves, so "the outermost
  wins" turns a robed person's legs into one wide cylinder (Jauffre's
  calves 14.5 units round). Hand-authored colliders (B&S) would leave the
  cloth out; a generated set needs a rule for cloth.
