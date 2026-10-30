-- MobyRotateToAngle's 5th argument at Wizard Peak (docs/upstream-notes.md).
-- UpdateMoby_LEVEL_33 calls MobyRotateToAngle with 4 arguments, so the
-- callee reads `continueRotation` from the stale word at 16($sp). This
-- drives the title menu into a new game with a host pad script ($ORACLE_PAD,
-- shifted by $ORACLE_PAD_SHIFT vblanks: the emulator reaches the title
-- 1520 vblanks after the host, as its BIOS boots first), warps
-- from Artisans to Wizard Peak (level 33) through the game's unused level
-- warp cheat (open the inventory, set g_LevelCheatActive, then TRIANGLE
-- TRIANGLE: world 3, level 3; cheats.c CheatProcessLevelWarp), then logs every 4-argument call from the overlay: return address and
-- the word at 16($sp). Output: $ORACLE_OUT/wizard.txt. Quits at $ORACLE_TO.
local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local out = assert(os.getenv('ORACLE_OUT'))
local to = tonumber(os.getenv('ORACLE_TO') or '12000')
local function p32(a) return ffi.cast('int32_t*', mem + bit.band(a, 0x1fffff)) end
local function r32(a) return p32(a)[0] end
local function w32(a, v) p32(a)[0] = v end

local GAMESTATE, LEVEL, NEXT_LEVEL = 0x800757D8, 0x8007596C, 0x800758B4
local log = assert(io.open(out .. '/wizard.txt', 'w'))
local function say(fmt, ...)
  local s = string.format(fmt, ...)
  log:write(s, '\n'); log:flush(); print(s)
end

local B = PCSX.CONSTS.PAD.BUTTON
local pad1 = PCSX.SIO0.slots[1].pads[1]
local names = { start = B.START, cross = B.CROSS, circle = B.CIRCLE, square = B.SQUARE,
  triangle = B.TRIANGLE, up = B.UP, down = B.DOWN, left = B.LEFT, right = B.RIGHT }
local script, si, held = {}, 1, {}
local shift = tonumber(os.getenv('ORACLE_PAD_SHIFT') or '1520')
local padpath = assert(os.getenv('ORACLE_PAD'), 'ORACLE_PAD not set')
for line in io.lines(padpath) do
  local v, rest = line:match('^%s*(%d+)%s+(.*)$')
  if v then script[#script + 1] = { tonumber(v) + shift, rest } end
end
local function setpad(spec)
  for b in pairs(held) do pad1.clearOverride(b) end
  held = {}
  for w in spec:lower():gmatch('%w+') do
    if names[w] then held[names[w]] = true; pad1.setOverride(names[w]) end
  end
end
local vblank, arrived, warped = 0, nil, false
local tally = {}

bp = PCSX.addBreakpoint(0x80038EE0, 'Exec', 4, 'MobyRotateToAngle', function()
  if r32(LEVEL) ~= 33 then return true end
  local g = PCSX.getRegisters().GPR.n
  if g.ra < 0x8007A000 or g.a2 ~= 4 or g.a3 ~= 0x10 then return true end
  local slot = r32(g.sp + 16)
  local key = string.format('ra %08x slot %08x', g.ra, bit.band(slot, 0xffffffff))
  if not tally[key] then say('vblank %d first: %s (moby %08x angle %d)', vblank, key, g.a0, g.a1) end
  tally[key] = (tally[key] or 0) + 1
  return true
end)

vsyncListener = PCSX.Events.createEventListener('GPU::Vsync', function()
  vblank = vblank + 1
  local gs, lvl = r32(GAMESTATE), r32(LEVEL)
  if not arrived then
    if gs == 0 and lvl == 10 then
      arrived = vblank
      setpad('none')
      say('vblank %d: playing in Artisans', vblank)
    else
      while si <= #script and script[si][1] <= vblank do setpad(script[si][2]); si = si + 1 end
    end
  elseif not warped then
    -- Pause, unlock the warp cheat, pick world 3 then level 3.
    local t = vblank - arrived
    -- Pause, Down, Down, Cross opens the inventory (gamestate 3), where
    -- func_8002EB2C runs CheatProcessLevelWarp while g_LevelCheatActive.
    local steps = { [120] = { B.START, true }, [126] = { B.START, false },
      [180] = { B.DOWN, true }, [186] = { B.DOWN, false },
      [220] = { B.DOWN, true }, [226] = { B.DOWN, false },
      [260] = { B.CROSS, true }, [266] = { B.CROSS, false },
      [320] = 'cheat', [340] = { B.TRIANGLE, true }, [346] = { B.TRIANGLE, false },
      [380] = { B.TRIANGLE, true }, [386] = { B.TRIANGLE, false } }
    local st = steps[t]
    if st == 'cheat' then
      say('vblank %d: gamestate %d (3 is the inventory), g_LevelCheatActive = 1', vblank, gs)
      w32(0x80075880, 1) -- g_LevelCheatActive
      w32(0x800758EC, 0) -- cheat_HomeworldSelected
    elseif st then
      if st[2] then pad1.setOverride(st[1]) else pad1.clearOverride(st[1]) end
    end
    if t == 400 then
      warped = true
      say('vblank %d: gamestate %d, next level %d', vblank, gs, r32(NEXT_LEVEL))
    end
  end
  if vblank % 600 == 0 then say('vblank %d gamestate %d level %d', vblank, gs, lvl) end
  if vblank >= to then
    for k, n in pairs(tally) do say('%s calls %d', k, n) end
    log:close()
    PCSX.quit(0)
  end
end)
