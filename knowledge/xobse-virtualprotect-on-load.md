# xOBSE 22.13: VirtualProtect around 0x00585C90 dominates the first frame after a load

Status: measured (2026-10-28, Linux/Proton, Nadi's Windows mod list mirrored; OBVR perf-load-scripts.txt, sampler).

- Measured: the first world frame after loading a save took 2315 ms of scripts (32,006 runs of 602 scripts; Maskar's Oblivion Overhaul's scripts the most expensive). 2061 sampler samples in that frame.
- Measured: ~1100 samples (about half) innermost in ntdll via kernelbase, called from obse_1_2_416.dll at RVA 0x22F7A / 0x61CCC: `push &old; push 0x40|old; push 4; push 0x00585C90; call esi` = VirtualProtect on 4 bytes at 0x00585C90, around a write of 0xC2 (`ret`) / the original byte there. Read: 0x00585C90 is a function entry (push -1; push 0x9BF2B6; SEH frame; sub esp,0x834). Derived (unverified): xOBSE disables that function for the duration of a command (a "no message" variant) and re-protects each time.
- Measured: ~400 samples (a fifth) in the engine's wait loop at 0x004321B0 (`push 1; call Sleep; cmp [edi],3; je`) reached through xOBSE RVA 0xB03F (`call [eax+0x150]`).
- Unverified: the Windows cost of the same VirtualProtect calls (Wine's is a syscall path; Windows' is cheaper but still a kernel transition), and which xOBSE command does it.
