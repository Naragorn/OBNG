# Function ledger

States: `mapped` (map stage done), `confirmed` (refutation held), `disputed`
(refutation did not hold after one fix round), `reconstructed` (rebuilt,
tests green), `review-failed` (rebuild did not pass review after one fix
round), `verified` (in-game differential check clean).

| Address | Name (proposal) | Slice | State | Run | Note |
|---|---|---|---|---|---|
| 0x0047D170 | TimeInfo::Update(UInt32 tickMs) | frame-clock | disputed | wf_b24db2af-f80 (2026-10-26) | Instruction-level reading held with every skeptic (thiscall, ret 4, frozen/fixed/variable paths, clamps, FSUBP carry). Disputed on derived overstatements ("first unfrozen frame reads as 166 ms" only in variable mode past the ceiling; the carry sum is rounded to float each frame, so "exactly the step" is false) and on a stale cross-file citation. Constants 0x00A2FC78, 0x00A30E40, 0x00A3D14C not in the export. Not rebuilt. |
| 0x0040D800 | OSGlobals::UpdateStep | frame-clock | disputed | wf_b24db2af-f80 | Map only, by design (524 instructions, 106 calls, 14 IsMenuMode calls). Disputed on: 0x00889810 at 0x0040D8C2 is a direct callee in the Havok range receiving dt (claim "no Havok step is a direct callee" overreached); g_worldSceneGraph+0xDC is the camera (OBVR GameAddresses.h:1056, xOBSE NiObjects.h); InterfaceManager+0x08 is UInt32 currentMode (xOBSE GameAPI.h:498), read as a byte at 0x0040D9EC. No callers in the export; OBVR's 0x0040D6F7 is the candidate. |
| 0x005AD980 | LoadingMenu::Tick | frame-clock | review-failed | wf_b24db2af-f80 | Confirmed in round 2. Menu type 0x3EF (kMenuType_Loading, xOBSE GameMenus.h); the menu owns a private TimeInfo at this+0x54 and calls 0x0047D170 on it. Rebuilt with 20 test flows, green; rejected by the correctness review: the countdown matches the original only at x87 precision control 53-bit, inputs differ at 24-bit (timer 5.0, sinceLast -100). The game's control word is unverified - a shadow-mode read settles it. |
| 0x005AD0D0 | LoadingMenu::LoadingMenu | frame-clock | review-failed | wf_b24db2af-f80 | Confirmed in round 1 (behaviour, consistency held; fidelity refuted this+0x68 as uint32 where the compares are signed). Allocates its TimeInfo with FormHeap_Allocate(0x18) at 0x005AD2E7 and constructs it via 0x0047D150; this may be NULL after a failed allocation (XOR EDI,EDI at 0x005AD315). Rebuilt, 11 flows, green; blocker in review: the warning string ends in CR LF in the binary, the rebuild writes LF - the export's JSON helper dropped the CR (tools/ghidra/ExportFunctions.java, fixed after the run). |
| 0x004949E0 | GameMessageBoxA | frame-clock | disputed | wf_b24db2af-f80 | __stdcall(text, caption, type), ret 0xC. Brackets the clock with 0x0047D120 / 0x0047D0F0 on g_timeInfo. Disputed because the choice-to-result mapping (0->6, 1->7, 2..5 identity) comes from a jump table at 0x00494E70 that lies past the body and is not in the export; the export has no callers either. |
| 0x004E1580 | TESObjectREFR::LoadBaseFormModels | frame-clock | reconstructed | wf_b24db2af-f80 | Confirmed by all three skeptics in round 1. Brackets the clock with the 0x0047D120 / 0x0047D0F0 pair while loading a reference's models (TESObjectREFR +0x1C baseForm, +0x3C niNode; TESForm +0x04 typeID). 24 test flows, both reviews approved after one tests fix. Not verified in the game. |
| 0x009DC2D0 | StaticInit_RegisterIniSetting_B06704 | frame-clock | reconstructed | wf_b24db2af-f80 | Static initialiser: registers the SettingInfo at 0x00B06704 (value +0x00 float, name pointer +0x04 = 0x00B06708) with the IniSettingCollection 0x00B07BF0 via 0x004040E0 (thiscall, ret 4) and arms an atexit. So the time multiplier is an INI setting object, not a bare float. 7 flows, approved after one fix round. Which setting it is: not in the export. |

Open from the slice (next slice candidates): the TimeInfo class itself -
0x0047D150 (constructor, tick argument), 0x0047D120 / 0x0047D0F0 (the
freeze/unfreeze pair, or a nesting counter), the writers of +0x00 and +0x04
(iFPSClamp), the readers of 0x00B39DB0 / 0x00B39DB4 (this and the previous
tick, shared by every instance), the code at 0x009DC2B6 that writes
g_timeInfo, and the caller of 0x0040D800.
