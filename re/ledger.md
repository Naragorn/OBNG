# Function ledger

States: `mapped` (map stage done; map-only functions end here), `confirmed`
(refutation held), `disputed` (a core refutation did not hold after one fix
round), `reconstructed` (rebuilt, tests green), `review-failed` (rebuild did
not pass review after one fix round), `verified` (in-game differential check
clean). Since the time-info run a mapping whose core held but whose derived
claims were trimmed is marked "trimmed".

| Address | Name | Slice | State | Run | Note |
|---|---|---|---|---|---|
| 0x0047D170 | TimeInfo::Update | time-info (first mapped in frame-clock) | reconstructed, trimmed | wf_ef0eca3a-895 (2026-10-27) | Core held with all three skeptics in both runs. Rebuilt with 17 test flows; the tests check the emulator's bit patterns (tools/difftest/expected/0x0047D170.json: 0x3C54FDF4, 0x3A83126F, 0x3E29FBE8, 0x3C88DFEB, 0x3F355560, 0x3BD4FDF4) and both reviews approved first time. Writes 0x00B39DB0 = tick - startTick (not a raw tick) and 0x00B39DB4 = the previous value. The fix-up at 0x0047D231 is unreachable (derived). Open: who writes +0x04; the game's x87 control word. |
| 0x0047D150 | TimeInfo::Init | time-info | reconstructed, trimmed | wf_ef0eca3a-895 | thiscall(tick), ret 4: zero byte to +0x00, 0.0f to +0x04/+0x08/+0x0C, 0 to +0x10, tick to +0x14; +0x01..+0x03 untouched. EAX returns the tick, not this - constructor versus init method is open. 9 flows, approved. |
| 0x0047D120 | TimeInfo::Freeze | time-info | reconstructed, trimmed | wf_ef0eca3a-895 | ++freezeDepth (ADD CL,1 at 0x0047D127); on the 0->1 transition GetTickCount once and +0x10 := now - +0x10, +0x14 := now - +0x14. Nested entries only count. 7 flows, approved. Open: the unbalanced call sites at 0x004061AF / 0x0040616E (no Ghidra function). |
| 0x0047D0F0 | TimeInfo::Unfreeze | time-info | reconstructed, trimmed | wf_ef0eca3a-895 | --freezeDepth when nonzero; on 1->0 the same now - field rewrite of +0x10 and +0x14, so across one outermost bracket both fields come out shifted by the frozen stretch D. One skeptic (fidelity) raised a core refutation, two did not, so no fix round; its point went into the trim. 7 flows, approved. Open: whether an Update runs on g_timeInfo inside a bracket (GameMessageBoxA's message loop). |
| 0x009DC2A0 | TimeInfo_BuildGlobal | time-info | reconstructed, trimmed | wf_ef0eca3a-895 | void(): Init's stores inlined on 0x00B33E90 with GetTickCount (import slot 0x00A280D0) as the start tick. No callers in the export; that it is a CRT initialiser entry is unverified (the dword a0 c2 9d 00 in __xc_a..__xc_z would settle it). 8 flows, approved. |
| 0x0055D4C0 | BSFaceGenNiNode::UpdateDownwardPass (proposal) | time-info | mapped, trimmed | wf_ef0eca3a-895 | Map only (300 instructions, 9 callees). A NiNode subclass's update override: reads the two clock globals, hands their difference in seconds (times (double)0.001f) to the FaceGen animation object - no clamp, no fGlobalTimeMultiplier. Layout agrees with xOBSE NiObjects.h BSFaceGenNiNode; vtable slot unverified. Function created from Ghidra's gap after 0x0055D1B0. |
| 0x00889810 | PlanHavokSteps (proposal) | time-info | mapped, trimmed | wf_ef0eca3a-895 | Map only (143 instructions, calls nothing). A scheduler over globals: frame seconds + carry 0x00BA7910 -> step count 0x00BA7914 and step length 0x00BA790C, by update type 0x00BA7918 (0 = fixed 1/60 step at 0x00B2E2E8 with carry clamped to one step; 10 = smoothed; else equal split), step cap 3 or 2 (flag 0x00B333B8). Compares the frame seconds against the 166.67 constant 0x00A3D14C - a millisecond value used as a seconds limit (unverified why). Static reading conflicts with OBVR's measured "60 Hz physics at 90 fps, one frame in three without a step" (hand-weapon-collision-spec.md:636-646); unresolved. |
| 0x0040D800 | OSGlobals::UpdateStep (proposal) | frame-clock | disputed | wf_b24db2af-f80 (2026-10-26) | Map only, by design (524 instructions, 106 calls, 14 IsMenuMode calls). Disputed on: 0x00889810 at 0x0040D8C2 is a direct callee receiving dt (now mapped: PlanHavokSteps); g_worldSceneGraph+0xDC is the camera (OBVR GameAddresses.h:1056, xOBSE NiObjects.h); InterfaceManager+0x08 is UInt32 currentMode (xOBSE GameAPI.h:498), read as a byte at 0x0040D9EC. No callers in the export; OBVR's 0x0040D6F7 is the candidate. Under the pilot's stricter criterion; not re-run. |
| 0x005AD980 | LoadingMenu::Tick | frame-clock | review-failed | wf_b24db2af-f80 | Confirmed in round 2. Menu type 0x3EF (kMenuType_Loading, xOBSE GameMenus.h); the menu owns a private TimeInfo at this+0x54 and calls 0x0047D170 on it. Rebuilt with 20 test flows, green; rejected by the correctness review: the countdown matches the original only at x87 precision control 53-bit, inputs differ at 24-bit (timer 5.0, sinceLast -100). The game's control word is unverified - a shadow-mode read settles it. |
| 0x005AD0D0 | LoadingMenu::LoadingMenu | frame-clock | review-failed | wf_b24db2af-f80 | Confirmed in round 1 (fidelity refuted this+0x68 as uint32 where the compares are signed). Allocates its TimeInfo with FormHeap_Allocate(0x18) (call at 0x005AD2E9) and initialises it through 0x0047D150; this may be NULL after a failed allocation (XOR EDI,EDI at 0x005AD315). Rebuilt, 11 flows, green; blocker in review: the warning string ends in CR LF in the binary, the rebuild writes LF - the export's JSON helper dropped the CR (fixed 2026-10-27; the rebuild not yet corrected). |
| 0x004949E0 | GameMessageBoxA | frame-clock | disputed | wf_b24db2af-f80 | __stdcall(text, caption, type), ret 0xC. Brackets the clock with TimeInfo::Freeze / Unfreeze on g_timeInfo. Disputed because the choice-to-result mapping (0->6, 1->7, 2..5 identity) came from a jump table at 0x00494E70 past the body, not in the pilot's export; the export now carries jump tables (data.json). Not re-run. |
| 0x004E1580 | TESObjectREFR::LoadBaseFormModels | frame-clock | reconstructed | wf_b24db2af-f80 | Confirmed by all three skeptics in round 1. Brackets the clock with Freeze / Unfreeze while loading a reference's models (TESObjectREFR +0x1C baseForm, +0x3C niNode; TESForm +0x04 typeID). 24 test flows, both reviews approved after one tests fix. Not verified in the game. |
| 0x009DC2D0 | StaticInit_fGlobalTimeMultiplier | frame-clock | reconstructed | wf_b24db2af-f80 | Static initialiser: registers the SettingInfo at 0x00B06704 (value 1.0f, name pointer 0x00A3D150 "fGlobalTimeMultiplier:General") with the IniSettingCollection 0x00B07BF0 via 0x004040E0 (thiscall, ret 4) and arms an atexit. 7 flows, approved after one fix round. |

Decisions taken after the time-info run (the skeptics' needsDecision list):
TimeInfo field names are `freezeDepth`, `fixedStepMs`, `fixedStepCarryMs`,
`frameSeconds`, `clockMs`, `startTick` (src/engine/time/TimeInfo.h); the
clock globals are `g_timeInfoClockMs` / `g_timeInfoPreviousClockMs`; the
166.67 constant is `kFrameCeiling` (unit-neutral, used as ms and as s);
0x00A30E40 is (double)0.001f; the bracket pair is Freeze / Unfreeze;
0x0047D150 is `Init` until a constructor is shown. The rebuilt sources and
tests were renamed accordingly and rebuilt.

Open from both slices: the writer of +0x04 (the fixed step; iFPSClamp is
the guess), the function around 0x00406169 / 0x004061AA (calls the bracket;
no Ghidra function, 0x004060AC is not code), the caller of 0x0040D800, the
game's x87 control word, whether 0x009DC2A0 sits in the CRT initialiser
table, and PlanHavokSteps against OBVR's measured physics rate.
