# The stats menu's info box: a tile of the stats menu, known to the engine by its id

Status: knowledge, written 2026-11-25 from OBVR (`../OBVR`: `src/vr/TabletLogic.h`
`PagePopupTileId`, `src/game/HudTiles.cpp` `ReadMenuPaper`, docs/inventory-tablet-spec.md
section 22). Nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (the bytes disassembled with objdump) or in the
game's own files, **measured** = measured in the game (OBVR's hand-script harness), **derived** =
follows from the two, **unverified** = not checked.

## Which box (read)

- The inventory's and the spells' info text beside the page is a menu of its own, MagicPopupMenu
  (`menus/main/magic_popup_menu.xml`). The stats menu does not use it: an attribute's or a
  skill's description is shown by a picture of the stats menu itself,
  `stat_pop_background` in `menus/main/stats_menu.xml` (Oblivion - Misc.bsa), a child of the
  menu's root with `<id> 61 </id>`.
- Its layout, all in the XML: 419 wide, its height the text's lower edge plus 25; y 170;
  x = stat_layout.x + stat_background.x + stat_background.width - width * (1 - user0) - 20. It
  slides out from behind the page's right edge as its user0 goes from 0 to 1 (user1 = 0.24, the
  slide's seconds) and is clear (alpha 0) while user0 is 0. Its depth is -9, the page's 4: the
  part still left of the page's edge is behind the page. At rest it starts 20 units inside the
  page's right edge and ends 399 beyond it. user2 is the icon, user3 the text, user4 whether the
  icon shows, user5 the hand-to-hand damage (shown from 0 up on the skills' tab).

## How the engine finds it (read)

- The stats menu's vtable is at 0x00A6D9DC (the menu's destructor 0x005D9D70 writes it). Slot 1,
  Menu::SetField(id, tile) in xOBSE's naming, is 0x005D9BE0: a chain of compares on the id that
  stores the tile pointer into the menu object.

  | id | kept at | what (the vanilla XML's name) |
  |---|---|---|
  | 10 | +0x28 | stat_p1, the first tab's page |
  | 20 | +0x2C | (the second tab's page) |
  | 30 | +0x30 | (the third tab's page) |
  | 40 | +0x34 | (the fourth tab's page) |
  | 50 | +0x38 | (the fifth tab's page) |
  | 31, 32 | +0x3C, +0x40 | tiles of the third tab |
  | 41, 42 | +0x44, +0x48 | tiles of the fourth tab (+0x44 is the pane the faction rows are made in: 0x005DC950 instantiates "stat_faction_template" under it) |
  | 51, 52 | +0x4C, +0x50 | tiles of the fifth tab |
  | 60 | +0x54 | stat_focus_box |
  | 61 | +0x58 | stat_pop_background, the info box |

  The names in brackets are derived from the id pattern, not read tile by tile.
- 0x005D9B80 answers whether all thirteen are set; the menu's creation (0x005DCB70, which loads
  `Data\Menus\Main\stats_menu.xml` and checks the menu's class id 0x3EB) goes on only then.
- Derived: a UI mod's stats menu has to keep these ids - a tile the engine cannot find by id
  leaves the pointer null and the menu refuses to build - but is free in names and layout. Code
  that wants the info box in any UI finds it by id 61, not by the name.

## What a modern engine does instead

- A menu's parts bound by name or by a typed handle the layout file declares, checked when the
  layout loads with an error that names the missing part; popups as their own layer with their
  own bounds, so nothing outside has to tell a page's popup from the page by its size.

## What the harness saw (measured)

- OBVR's hand-script harness, 2026-11-25 (Linux, vanilla menus, a 16:9 interface of 1706.7 by
  960 units; `../OBVR` artifacts/hand-script/tablet-stats-aim/20261125-083055-slot3): with the
  cursor on a row of the stats page the tile of id 61 becomes a drawn picture (alpha above 0)
  and its right edge runs out from 798 to 1173 units - the page stands at 27..772, so 401 units
  beyond the page's right edge at rest, 399 of the box and 2 of its border's corner - and back
  the same way when the cursor leaves the row. That is the XML's slide; the 0.24 s were not
  timed.
- Derived from the XML, not logged in the run: down to the third level under the menu's root
  the shown pictures are the page (stat_background), its right edge strip (stat_background_2)
  and, while it shows, the box with its border and content pictures. A reader that takes "the
  menu's large pictures" for the page misses the box unless its text is long: 419 wide against
  745 by 792, a box under 563 units high is under 0.4 of the page's area. That was OBVR's
  tablet until 2026-11-25 - the box cut off at the page's edge.
