# Game time and placed books

Status: knowledge, written 2026-10-26 from OblivionWalkers' Black Horse
Courier news (`../OblivionWalkers`, src/News.cpp, src/NewsLogic.h, commits
6ab82fd and 92865e5; docs/walkers-spec.md 4.9). Nothing of this is built in
OBNG.

## The game clock (Oblivion.exe 1.2.0.416, Oblivion.esm)

- read: the time globals of Oblivion.esm are GLOBs `0x35` GameYear, `0x36`
  GameMonth, `0x37` GameDay, `0x38` GameHour, `0x39` GameDaysPassed, `0x3A`
  TimeScale (ESM values 433, 7, 27, 1.0, 1.0, 30.0; all FNAM `s`).
- read: a TESGlobal keeps its value as a float at `+0x24` (xOBSE
  GameForms.h: name `+0x18`, type `+0x20`, data `+0x24`).
- measured: at runtime GameDaysPassed counts **whole days only** - it read
  1.000 in three harness runs while GameHour ran from 12.927 to 13.503
  (2026-10-26 20:03 to 20:10, Linux headless). A clock in days is
  `floor(GameDaysPassed) + GameHour / 24`.
- measured: in the main menu, before a save is loaded, the globals hold
  the ESM values (GameHour 1.000).
- unverified: whether GameDaysPassed and GameHour change in the same frame
  at midnight.

## A placed book's base, swapped at runtime

- read: a reference's base form pointer is at `+0x1C` (TESObjectREFR).
- measured: writing another BOOK there for a placed book whose base has the
  same model changes what the engine shows: the crosshair's HUD name is the
  new book's FULL, and activating it opens the new book's DESC with its
  `<IMG>` header (harness news-courier 20:07 and 20:10: the Courier's
  office broadsheet the coc marker faces; which of its 20 references that
  is was not logged). The 3D is not rebuilt, so a different model would
  keep the old mesh until the cell reloads (derived, not tested).
- measured: persistent references (record flag `0x400`) are in the form
  table from the start - found by LookupFormByID (`0x0046B250`) in the main
  menu already; references that are not persistent only once their cell is
  loaded, and they stay found after the player leaves (Count's Arms in
  Anvil, still found out in Tamriel; derived: the cell stays cached).
- unverified: whether a save keeps a swapped base; OblivionWalkers sets it
  again within a second of a load either way.

## The console and books

- measured: the console's `activate player` opened no book in the harness
  (OBVR's HandScriptConsole lines, run through xOBSE's console interface) -
  neither `000564A5.activate player` (not persistent; also with `prid`
  first) nor `000738E4.activate player` (persistent). The player's own
  activation (OBVR's right-hand A on the HUD target) opened it.
- unverified: why. A common modding rule says a console line names only
  persistent references by ID, `prid` being the way to the others - not
  checked here, and the persistent one did not open either.
