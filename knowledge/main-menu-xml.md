# The main menu's XML: what its user traits are, and adding a button to it

Status: knowledge, written 2026-11-16 from OBVR (a "VR" button put into the
loaded main menu). Marks: **read** = seen in the file or the EXE's data;
**measured** = seen in the game; **derived**; **unverified**.

## The file

- **Read** (`menus\options\main_menu.xml` in `Oblivion - Misc.bsa`, Steam
  1.2.416; 6554 bytes): menu `MainMenu`, class `&MainMenu;` (menu type
  1044), `<stackingtype> &no_click_past;`. Its own traits, each with the
  file's comment:
  - `user0` "is the logo visible set by code"
  - `user1` "are the buttons visible set by code"
  - `user2` "is Press Start visible set by code" (Xbox)
  - `user3` "alpha value set by animated code" - used once, as the alpha of
    the inner `tes_oblivion_logo_bink.dds` image laid over the real logo
  - `user4` "how many seconds to fade from bink logo to real logo" (3)
- **Read:** the buttons sit in `<rect name="button_layout">` (visible by
  `user1`, depth 10, y 725, centred on the screen's width): `main_continue`
  (id 2), `main_new` (3), `main_load` (4), `main_options` (5),
  `main_credits` (6), `main_exit` (7), each `<include
  src="button_floating.xml"/>` (prefab: target, width = its text + 40,
  height 64, the highlight `Menus\Dialog\dialog_selection_full.dds` shown
  while `mouseover` is 1, the text 117/59/33 at alpha 255). The backdrop is
  id 8, the version text id 9 (x 10, y 935).
- **Measured** (OBVR harness, Linux, main-menu-vr-button/20261116-100010
  and vr-button-sweep/20261116-100344): a tile whose alpha copies
  `MainMenu`'s `user3` is invisible once the menu stands - the fade has run
  to 0; the same tile with alpha 255 shows. So `user3` is the film logo's
  fade only; nothing else in the menu fades by it, and the buttons are
  switched by `user1` alone (**derived** from the file: no other use).

## A tile added at run time

- **Measured** (the same runs, MenuQue v16b under xOBSE): the console line
  `InsertXML "OBVR\main_vr_button.xml" 1044` (a file under
  `Data\Menus\Prefabs`, the component left out = the menu's top level)
  answers 1 and the tile is there: it takes the cursor (`mouseover`, the
  interface's active tile is the new rect) and a click on it is handed to
  the main menu's click handler with its id.
- **Measured:** an id the main menu's handler does not know (9700) does
  nothing there - no crash, no menu opened (main-menu-vr-button/
  20261116-100657). Where the handler switches on the id is not read.
- **Measured:** a rect that includes `button_floating.xml` and sets its own
  `string`, `x` and `y` after the include draws as the menu's own buttons
  do, with their highlight under the cursor.

## Modern engines

A menu is a tree of widgets with named slots that mods extend through an
API (e.g. a UI toolkit's extension points); nothing is spliced into a
parsed XML tree by a console command (unverified here; no source fetched).
