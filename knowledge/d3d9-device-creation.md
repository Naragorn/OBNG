# Direct3D 9 device creation: two factories, and a Windows shim that re-hooks the factory

Status: read in the EXE and in apphelp.dll (2026-11-21); the shim's presence observed once in OBVR's log (Windows run of 2026-11-20); what makes Windows attach the shim not established.

## What the engine does (read, decompiled export rea-all)

- 0x0076BD40 makes the renderer: 0x00761DF0 first, then 0x0076A1C0.
- 0x00761DF0 makes the main factory once: `LoadLibraryA("D3D9.DLL")` into 0x00B42150, `GetProcAddress(.., "Direct3DCreate9")` into 0x00B42158 (the cached pointer), `Direct3DCreate9(0x20)` into 0x00B42154 ("Create D3D9 instance...SUCCESSFUL").
- 0x0076A1C0 calls 0x00763DE0 first (decompiled line 67), which makes a second factory through the same cached pointer 0x00B42158, wraps it (0x00775DF0, kept at 0x00B42160) and releases it. 0x004980D0 and 0x00498340 call 0x00763DE0 too (kept at 0x00B34FC4).
- 0x0076A1C0 then creates the device through the main factory: `(*DAT_00b42154)->CreateDevice` (vtable +0x40, slot 16), at two call sites (decompiled lines 153 and 272: the first try and a fallback). Instruction addresses not read yet.
- So two IDirect3D9 objects exist before the device is made. With DXVK (and Microsoft's runtime) they share one method table.

## What a Windows compatibility shim does with it (read, SysWOW64\apphelp.dll of 2026-10-28)

- The detector shim DetectorDWM8And16Bit (layers DWM8And16BitMitigation, ApplicationMonitor, DetectorsXP/Vista/Win7, Win7RTM/Win8RTM...) hooks Direct3DCreate9 and registers every factory it returns with the shim engine's COM router (`SE_COM_HookObject` 0x10066600 -> 0x10083C3E "SeComRouterHookObject"), which patches the shared method table: QueryInterface, Release and `DWM8AND16BitCOMHook_IDirect3D9_CreateDevice` (0x10080DF0) into slot 16.
- The patch helper 0x100842A2 keeps a list of {table, slot, original, hook}. When the slot no longer holds its hook (someone else patched it after), it adds a new record with the current pointer as "original" and writes its hook again.
- The hook calls on through `SE_COM_Lookup` (0x10066680 -> 0x10083FDC), which returns the FIRST record's original for {table, hook} - the pointer that was there before anyone else.
- Consequence (derived from the three functions): any hooker that patched slot 16 between the first and the second factory is cut out of the chain - the second factory's registration puts the shim's hook back in front, and the shim calls the runtime's original directly. In the engine's order (main factory, then the adapter factory in 0x00763DE0, then CreateDevice) this happens every time the shim is present.

## Observed in OBVR (Windows, 2026-11-20 21:37)

- OBVR patches slot 16 of the first factory it sees; in this run its CreateDevice was never called: the back buffer was made at the game's 2560x1440 instead of 8072x3376, fullscreen with the game's vsync. OBVR's log named the slot's owner afterwards: 57BB0DF0 in apphelp.dll. 0x57BB0DF0 - 0x80DF0 = 0x57B30000; of the eight 64 KB-aligned bases that put the address in .text, only this one lands on a function entry, and that function is the CreateDevice hook above (derived).
- The game was started by running obse_loader.exe from Explorer at that second (C:\Windows\appcompat\pca\PcaAppLaunchDic.txt: 2026-11-20 20:37:22 UTC).
- Unverified: why the shim was in this process and not in earlier runs started the same way (PcaGeneralDb0.txt shows obse_loader.exe tracked by the Program Compatibility Assistant on 2026-11-15, and those runs got OBVR's frame).

## What a modern engine does instead

- Make one factory, keep it, and create the device from it; query adapter data from the same object. A port that owns its renderer has no shared method table for a shim or an overlay to fight over.
- The engine's two CreateDevice call sites in 0x0076A1C0 are 0x0076A542 and 0x0076A5AF (`mov eax,[ecx+40h]` `call eax`, read). They are not free: oblivion_display_tweaks.dll holds 0x0076A542 and its return address 0x0076A547 (derived: it hooks the first). OBVR therefore stays in the method table and takes slot 16 back after every factory (OBVR render/CreateDeviceSlot.h, 2026-11-21).
