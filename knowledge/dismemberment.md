# Dismemberment: cutting off a head, a hand or a limb

Status: knowledge, written 2026-10-25 from OBVR (a question about
decapitation in VR combat). Nothing of this is built, in OBVR or here.

## The task

A blade cuts through a neck, a wrist or a limb: the part comes off as its
own physical piece, the body and the piece both show a wound surface, and
the state survives a save.

## What Oblivion has

Nothing built in. Its skins are plain `NiSkinInstance`s; the dismemberment
data Bethesda added in its next games (below) does not exist in Oblivion's
meshes. OBVR's own specs say so as well (physical-combat-spec 198,
weapon-collision-spec 919).

### How the vanilla meshes are skinned (measured)

Read 2026-10-25 with pyffi 2.2.4.dev4 from `Oblivion - Meshes.bsa`
(`tools/bsa-extract.py` and `tools/nif-skin-bones.py` in the OBVR repo). A
vertex "belongs" to the bone with its largest weight; "shared" counts the
vertices whose largest weight is under 0.99.

| Mesh | Shapes | What decides a cut |
|---|---|---|
| `characters/imperial/headhuman.nif` | `_MedNeutral`, 1275 vertices, 5 bones | 1205 belong to `Bip01 Head`, 70 to `Bip01 Neck1`; about 180 shared between Head, Neck1, both Clavicles and Spine2. **The neck is part of the head mesh**, down to the collarbones. |
| `characters/imperial/mouthhuman.nif` | 45 vertices | not skinned (rides the head node) |
| `characters/_male/upperbody.nif` | `UpperBody` 475 vertices, 12 bones; `Arms` 410 vertices, 13 bones | the torso's neck ring is weighted to Neck1 and Head (41 + 41 vertices Head + Neck1 + Clavicle + Spine2). **Both arms are one shape**, upper arm to wrist (UpperArm, Twist, Forearm, ForearmTwist, 12 vertices on each Hand). |
| `characters/_male/hand.nif` | `Hand`, 1646 vertices, 36 bones | **both hands are one shape**, cleanly per side: hand and finger bones, 3 vertices on each ForearmTwist |

What follows (derived):

- Hiding a shape is not a cut: the head shape carries the neck, the arms
  shape both arms, the hand shape both hands.
- Freeing the head's ragdoll body alone stretches the neck: the
  Head-weighted vertices fly with it, the Neck/Clavicle/Spine2-weighted ones
  stay with the body. Blade & Sorcery players report the same artefact
  ("upon decapitation the neck stretches from the head to the body",
  https://mod.io/g/blade-and-sorcery/m/realistic-decapitation).
- A clean cut needs each worn skinned shape split into two triangle sets by
  the bone weights (the cut bone's side and the rest), plus a cap on each
  side. Armour and clothing are their own skinned shapes, so every one of
  them needs the same split - by weights this works for any mesh, a mod's
  included.

### A cut without touching the geometry (read in OBVR, built there for arms)

The split need not rewrite triangles. OBVR's arm stump (`src/game/ArmStump.h`,
built 2026-09-28) swaps bone pointers in a shape's own `NiSkinInstance`
(+0x14, one pointer per `NiSkinData` bone, the count at +0x40; xOBSE
`NiGeometry.h`) for its own nodes with almost no scale: every vertex hanging
from those bones collapses into one point and the skin tapers into it. The
GPU skins from those pointers every frame, so nothing is re-uploaded and no
vertex data is read. A cut is that swap on both sides - the body's shapes
send the bones beyond the cut to a collapsed node at the cut, the severed
part's shapes send the bones before it to one - plus a way for the severed
bone to leave (the ragdoll joint released). Designed for OBVR in
`OBVR/docs/dismemberment-spec.md`.

Derived for an engine pass: in Gamebryo the bone list of a skin instance is
the cheapest lever on a skinned mesh's shape; an engine with GPU skinning
can redirect, collapse or retarget parts of a mesh per instance with it,
which is what Unreal's `HideBoneByName` (bone scale 0) does in its own way.

### How a mod did it (Deadly Reflex, 2007-2010)

Deadly Reflex (Skycaptain) decapitates, cuts in half and cuts wrists. From
its readmes (http://www.gamesas.com/deadly-reflex-combat-moves-t73578.html,
https://www.moddb.com/mods/deadly-reflex): it works on NPCs only, not with
custom body meshes, cutting in half not with robes or whole-body clothing;
known issues name "two bodies" on some modded creatures, "an extra cape" on
a guard's head, and a neck "plug" too small for fat necks. Derived, not
stated by the author: it shows the parts through a copy of the actor plus
fixed plug meshes over the wounds. Version 6 needs NifSE (runtime NIF
editing through Niflib) and replaces the first-person skeleton and
animations.

### Not read in the EXE yet

The engine calls a runtime cut would need, none of them found:

- only if the cut is done on the geometry instead of the bone pointers
  (next section): cloning a skinned `NiTriShape` (Gamebryo's
  `NiObject::Clone` path) or rewriting its triangle list and re-uploading
  the index buffer;
- removing one constraint (the neck's `bhkRagdollConstraint`) from the
  Havok world while the ragdoll stays;
- whether other actors' vertex data stays on the CPU after the upload (the
  player's does: OBVR's BodyView reads it).

## How later engines do it

1. **Pre-authored partitions and caps (Bethesda, Fallout 3 onwards).**
   `BSDismemberSkinInstance` extends `NiSkinInstance` with per-partition
   body-part flags (https://geckwiki.com/index.php?title=BSDismemberSkinInstance,
   https://www.niftools.org/nifxml/BSDismemberBodyPartType.html): the mesh
   is split by the artist into `BP_HEAD`, `BP_LEFTARM`, ... partitions, with
   `BP_SECTIONCAP_*` gore caps on the severed part and `BP_TORSOCAP_*` caps
   on the body, hidden until the cut (`PF_EDITOR_VISIBLE` off), and
   `BP_TORSOSECTION_*` for the piece that stays on the torso. Skyrim keeps
   the scheme for its decapitation (`SBP_50_DECAPITATEDHEAD`,
   `SBP_51_DECAPITATE` neck gore). The runtime split by weights above is
   the same result computed instead of authored.
2. **Gore mesh plus a broken constraint (Unreal).** A second "gore" mesh
   has the limbs separated, the severed vertices weighted to one bone and
   caps over the holes; at the cut the normal mesh is hidden, the gore mesh
   shown, and `BreakConstraint(impulse, location, bone)` frees the part
   (https://forums.unrealengine.com/t/tutorial-preview-gore-mesh-dismemberment-tutorial/48172).
   `HideBoneByName` "just enforces a scale of 0 for the hidden bones", with
   `PBO_Term` terminating the physics bodies below
   (https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Components/SkinnedMesh/HideBonebyName).
3. **Physical colliders that may be sliced (Blade & Sorcery).** Ragdoll
   parts carry `sliceAllowed`; a blade passes a body only above
   `dismembermentMinVelocity` (OBVR weapon-collision-spec 17.1, from the B&S
   SDK wiki). In the base game a cut-off limb kills: the Survivable
   Dismemberment mod "makes dismemberment of arms and legs deal 25 damage
   instead of instant death" and keeps "decaps/beheadings will still kill"
   (https://www.nexusmods.com/bladeandsorcerynomad/mods/3389,
   https://vgtimes.com/games/blade-and-sorcery/files/29865-survivable-dismemberment.html).
   Players' reason why it is not in the base game: NPCs would need "an
   entirely separate set of animations and attacks" without an arm
   (https://www.reddit.com/r/BladeAndSorcery/comments/1dcaqhx/).

## Why dead first (derived)

On a dead actor the ragdoll already simulates every part: the severed
piece rides the ragdoll's own rigid body once the joint to it is released,
and nothing animates the body any more. On a living actor the skeleton is
driven by animation; a severed part needs its own copy of its bones and
its own rigid body, and the body left behind needs behaviour without the
part (a weapon in a hand that is gone, casting, AI, equipment slots). Every
engine above treats the cut as lethal by default for that reason.

## What this means for a reverse-engineering pass

- Find the three calls in "Not read in the EXE yet"; with them a
  `SeverAtBone(actor, bone)` helper - split by weights, caps sized from the
  bone's fitted radius, release the joint - covers head, hands and limbs at
  joints for every mesh.
- An OBNG-level alternative is the Fallout 3 route: teach the loader
  `BSDismemberSkinInstance`-style partitions so meshes can carry authored
  cuts and caps, with the weight split as the fallback for meshes without
  them.
