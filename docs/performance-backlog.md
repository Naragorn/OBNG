# Performance backlog

The aim (Nadi, 2026-11-10): at least 90 fps at 90 Hz on the Dream Air at
its resolution with every in-game detail option at its maximum, and beyond
that everything that can be had. The large items go first, ranked by their
share of the frame in an ordinary scene; what is put back because it is
small is listed here, with what was measured, so that it is picked up at
the end and not found again. Classes A, B, C: AGENTS.md.

## Where the frame stands (measured 2026-11-10)

Linux, the Beyond at 75 Hz, an eye of 2568x2568, every route of today on
(stereo draw, square root, skeleton update, volatile pack; run
obng-profile-gold-road-single/20261110-111908): every Gold Road phase holds
the 13.33 ms period (median 13.32-13.34; 0-0.2 % of the frames over 20 ms
at the spots and on the walks, 2 % across the loads between the spots).
Of a frame at the second spot the render thread waits 44 % (the poses
wait and a wait in the pass), the engine's own code is 31 % spread thin
(no function above 1.6 %), D3D9 9 %, OBNG's rebuilds 6 %, OBVR 4 %. The
scene pass is 2.4-4.1 ms, the application's GPU time before submit
5.2-6.7 ms.

Windows, the Dream Air at 90 Hz, an eye of 4036x3376 (2.07 times the
Beyond's pixels), the captures left in the game folder on 2026-11-09
(OBVR-performance-1-36 and -37, 129 and 118 s; which scene they are and
which routes were on is not recorded here - unverified):

| frame length | 36: frames | 36: time | 37: frames | 37: time | what fills it |
|---|---|---|---|---|---|
| up to 11.6 ms | 89.5 % | 80.9 % | 92.9 % | 88.7 % | busy 6.0-6.7 ms, the poses wait 4 ms |
| 11.6-13 ms | 9.0 % | 9.1 % | 6.1 % | 6.3 % | no single span long; the poses wait still 3-5 ms |
| 13-60 ms | 1.1 % | 1.6 % | 0.7 % | 1.2 % | busy 10-44 ms, the scene pass 3-14 ms |
| over 60 ms | 0.4 % (38) | 8.4 % | 0.3 % (33) | 3.8 % | 134-286 ms each; in 36 inside present (103 ms), in 37 outside every timed span |

The compositor's GPU numbers there: 3.4-3.8 ms before submit, 4.9-5.2 ms
in all (p95 6.5) - the GPU is not what limits these scenes.

What this says (derived): on the Gold Road the ordinary frame fits 90 Hz
with about 4 ms to spare on the Dream Air; what is lost is lost to the
long frames (a third of a percent of the frames, 4-8 % of the time), to
the frames that miss by a millisecond although the thread waited, and to
the scenes heavier than this one, which no capture here covers.

## Put back

| item | class | measured or derived gain | cost | state | where |
|---|---|---|---|---|---|
| Skeleton down passes on several threads | B | at best 1.6 ms with 97 actors, 0.3 ms with 34 (derived from 2.15 / 0.37 ms measured) | large; the collision objects must leave the down pass first, the controllers' statics shown by a write watch | the reordering is built (`[Route] ActorBatch`), one thread, not accepted | docs/subsystems/actor-batch.md |
| Character controller: no support check for a standing actor | C | about 10 us a standing actor: 0.4 ms in the crowd scenario, up to 1.3 ms with 134 standing (derived) | medium; a standing actor must still notice the ground moving away | not started | knowledge/threading.md |
| Character controller: far or unseen actors every second frame | C | by their share, not measured | medium | not started | knowledge/threading.md |
| Character controller on several threads | B | at best about 2 ms with 134 actors | large; the state machine and the contact callbacks may write into the world | not started | knowledge/threading.md |
| Volatile pack: the compare once a frame instead of once a pack | A | the compare is about 1.0 sample a frame in a crowd | small; needs a safe frame signal | put back | knowledge/skinned-vertex-streams.md |
| The cells' selected update (`[Route] GridUpdate`) | A | none measured against the original | built and verified | off | docs/subsystems/scene-update.md |
| The rest of the process manager's update outside the actor updates | ? | 1.5 ms with 117 actors, not looked into | small to look | not started | knowledge/threading.md |
| 0x00471C00 before ApplyAnimData | ? | 0.74-0.78 ms with 117 actors (6-7 us a call) | small to look | not started | knowledge/threading.md |
