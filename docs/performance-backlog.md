# Performance backlog

The aim (Nadi, 2026-11-10): at least 90 fps at 90 Hz on the Dream Air at
its resolution with every in-game detail option at its maximum, and beyond
that everything that can be had. The large items go first, ranked by their
share of the frame in an ordinary scene; what is put back because it is
small is listed here, with what was measured, so that it is picked up at
the end and not found again. Classes A, B, C: AGENTS.md.

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
