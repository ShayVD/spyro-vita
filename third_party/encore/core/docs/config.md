# config

`config.txt` in the app's data folder as `key=value` lines: `enc_config_read(path, fn, user)` calls `fn(key, value,
user)` for each. `#` lines and lines without `=` are skipped; the value loses trailing newline, CR and spaces; the
key is not trimmed. Pure stdio, so the same code runs on a host build.

**From:** kh1-decomp `host/vita/main.c` `config()` @ `e1575e1a` (its loop; the keys stay the game's).
**Test:** `core/test/config_test.c` (`core/test/run_tests.sh`).

## Keys per subsystem

One `fn` that knows every key (kh1-vita's `host/vita/main.c` grew to hundreds of keys) becomes the file every
change touches. The port autopilot never runs two workers on one file, so a central parser holds the whole queue
while one worker edits it (`playbooks/port-autopilot/SKILL.md`, Pitfalls: Hot files). Give each subsystem its own
key table in its own file from the start, and let `fn` walk the tables:

```c
/* gs_keys.c: the GS worker's keys, edited only by GS work */
const VpkKey gs_keys[] = {
    {"gsworker", VPK_KEY_INT, &gs_worker_on, "1: the GS worker thread (0 off)"},
    {"gsqmb",    VPK_KEY_INT, &gs_queue_mb,  "queue size in MiB"},
    {0}
};
/* config.c: the only list every subsystem adds one line to, once */
static const VpkKey *const tables[] = {gs_keys, snd_keys, ppp_keys, prof_keys, 0};
```

A new key is then one line in its subsystem's file, and an issue's `Files:` names that file instead of the parser.
`VpkKey` is the game's own type (the kit keeps only the line reader); keep each key's comment and default beside it,
so a table doubles as the key list for the docs and the console A/B queue.
