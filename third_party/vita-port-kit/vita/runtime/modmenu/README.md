# modmenu

A port's own options menu, the part that is the same in every game:

- **The table** (`vpk_menu.h`): each feature file lists its rows (`VpkMenuOption[]` with designated
  initialisers, and `VPK_MENU_LIST`): named values (optionally standing for other numbers), a number with a range
  and step, or an action. `vpk_menu_layout` lays the rows out by section (the game passes its section names),
  `vpk_menu_opt_text` / `vpk_menu_opt_change` show and step one, `vpk_menu_load` takes a `config.txt` line, and
  `vpk_menu_save` rewrites `config.txt` keeping every other line.
- **The panel** (`vpk_menu_panel.h`): the menu drawn on the CPU into an RGB image, with a 5x7 bitmap font made
  for it, section headers, the selected row's value between `< >`, and scrolling past 13 lines. It also draws a
  message screen (a disc that can't be used, before the game starts). The platform shows the image over the
  game, as a texture or straight into the picture.
- **The config.txt rewrite on its own** (`vpk_config_rewrite`), for a menu that keeps its own rows: in place (a
  key's first line rewritten where it is, later ones dropped, aliases) or appended (the keys' lines removed, a
  head, then every key).

What stays in the game: its rows and what they change, its sections' names, how the menu opens (a tap,
SELECT+START), pausing the game, and drawing the image.

**From:** spyro-vita `src/platform/common/menu_opts.c` / `menu_panel.c` @ `987363e` (its decisions D45); the
in-place rewrite from kh1-decomp `host/vita/menu.c`'s `save()` @ `85befa4a`. spyro-vita uses the table and the
panel; kh1-vita uses the rewrite, and keeps its own rows and its vita2d panel for now (several of its rows step
in ways the table has no field for: a cycle that skips a value without the GPU renderer, a tap that wraps the
volume).

**Test:** `test/menu_test.c` (spyro-vita's, game-neutral now): the panel at a few pixels (PPMs written for a
look), the table's layout, stepping, load and save, and both rewrite forms (`../test/run_host_tests.sh`).
