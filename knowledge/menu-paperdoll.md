# The menus' paperdoll: its scene, its camera, and reading the camera off a draw

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`: `src/render/MenuModelFit.h`,
`src/render/InterfaceRenderHook.cpp`, docs/controls-spec.md "Finery" and "the inventory's
figure blown up", `tools/hand-scripts/paperdoll-size.txt`). Nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (Ghidra over REA), **measured** = measured in
the game (OBVR's hand-script harness and Nadi's Windows log), **derived** = follows from the
two, **unverified** = not checked.

## The scene (read)

- The interface manager keeps a scene of its own for the figure, built by 0x0057E510: a root at
  +0x04, a shadow scene node at +0x64, the light "PlayerSceneLight" at +0x18 coloured by the
  fMenuPlayerLight... settings, and an NiNode at +0x60 that the player's model hangs on.
- The model is built by 0x00668D00, a method of an actor (its process read at +0x58; that the
  actor is the player is derived from the name) and kept at that object's +0x5D8: named
  "PlayerInventoryPC", a FaceGen-carrying copy of the biped with the weapon moved to the
  "Weapon" or "Torch" node, and with the HandToHand/TorchIdle animations loaded.

## The camera (measured)

- The scene's camera is at its origin, looking along +y with z up. The engine stands the
  holder 33.3 right, 168.0 ahead and 98.8 below that axis.
- The projection is 45.7 by 26.6 degrees (x scale 2.379, y scale 4.229), with a near plane of
  10 units. At that place the figure's feet sit 30.5 degrees below the axis, so the view cuts
  the figure at the hips. Why the engine stands it there is not read.

## Reading the camera off a draw (measured, derived)

- The figure is drawn inside the engine's 2D pass, with vertex shaders. Two constant layouts
  were seen there.
  - c0 to c3 are the world-view-projection, one row each: clip x, y, z, w. This is the body
    and the rigid pieces.
  - c1 to c4 are the projection alone, and c0 is something else.
- In the first layout the piece's own world rotation is baked into the rows. So c1.z is the
  y scale only for a piece whose z axis stands up, which is the body. One frame with the Iron
  Longsword sheathed read 4.229 from the body, and 0.075 and 0.027 from two pieces of the
  sword (measured, 2026-10-25 20:07). OBVR's fit once took the last draw's c1.z and stood the
  figure 2.6 units from the camera. Nadi saw that as the figure "extrem vergrößert" (derived).
- The y row's length divided by the w row's length is the y scale for every piece, at any
  rotation and any uniform scale. Both rows turn and scale with the piece: the y row is the
  view's up times the y scale, and the w row is the view's direction. All three draws read
  4.229 this way (measured).
- The rows belong to a perspective world-view-projection when the depth row runs along the
  view's direction (D3D's z = a w + b, with a = 1.001 here), and the x and y rows are square to
  it and to each other. The second layout fails that test (measured, from the Windows log of
  2026-10-24).

## For OBNG (derived)

A menu figure has a camera of its own, and the camera's numbers belong in one place the
interface can read. OBVR has to recover them from the shader constants because the engine
keeps them nowhere it has found (unverified: the renderer's current camera was not looked
for).
