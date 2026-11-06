# touch

The Vita's two touch panels, for what a port does with them:

- **The rear panel as extra buttons.** A PS1 / PS2 pad has L2 / R2 (and on the PS2 L3 / R3) that the Vita lacks.
  `vpk_touch_quarters` says which quarters of the panel the fingers are in, split at a raw point: kh1-decomp
  maps the four quarters to L2 / R2 / L3 / R3 split at (960, 500); spyro-vita maps the halves to L2 / R2 at the
  panel's middle (`vpk_touch_middle`).
- **A tap to open the port's own menu.** `VpkTap` judges one finger poll by poll: down and up within `max_us`,
  moving less than `max_move` on either axis (in the caller's units: kh1-decomp's half-size pixels, spyro-vita's
  0..1), and with `cancel_multi` no second finger. The caller decides where a tap counts (the middle of the
  screen, a menu row).

`vpk_touch_init` starts sampling on both panels and reads their active areas (the front panel's 1920 x 1088 when
that fails). `vpk_touch_read` gives a panel's fingers raw and as 0..1 across the active area, or -1 when the panel
could not be read (a game can skip that poll, or treat it as no fingers).

**From:** kh1-decomp `host/vita/pad.c` (the rear quarters) and `host/vita/menu.c` (its tap) @ `85befa4a`;
spyro-vita `src/platform/vita/pad.c` (panel areas, normalised fingers, the tap with a second-finger cancel; its
D46) @ `987363e`.

**Test:** `test/touch_test.c` against a fake sceTouch (`../test/fake_psp2/psp2/touch.h`): areas and the fallback,
fingers, a failed read, both games' quarter splits, both games' tap settings (`../test/run_host_tests.sh`).
