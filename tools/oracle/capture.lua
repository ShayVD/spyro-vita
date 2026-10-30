-- Function-level oracle capture (docs/porting-plan.md §7.3, layer 1).
-- For each function in $ORACLE_FUNCS ("ADDR NAME [OPTION...]" lines), take
-- up to $ORACLE_SAMPLES calls, at least $ORACLE_GAP vblanks apart, from vblank
-- $ORACLE_FROM on (options, to reach a function's other paths: n=N and gap=G
-- override those two; key=ADDR:SIZE gives each value of that RAM word its own
-- budget of N, such as one per Spyro state; if=ADDR:SIZE!=V or =V samples only
-- calls where it holds; SIZE is 1, 2 or 4 bytes, signed): the machine state on entry (NAME_K.in) and when the call
-- returns to its caller with the same $sp (NAME_K.out). The host replays each
-- .in through its translated C and compares with .out (spyro --replay,
-- tools/oracle/replay.sh). Quits at vblank $ORACLE_TO. With $ORACLE_TRACE,
-- also writes the per-vblank game-state TSV that the host's --trace writes
-- (src/game/trace.c), for tools/oracle/trace_diff.py. With $ORACLE_FUNCS
-- unset, only traces. $ORACLE_SHOTS ("V1,V2,...") or $ORACLE_SHOT_EVERY (N)
-- save the display at those vblanks (shot-V.raw: width, height, bpp words,
-- then pixels; tools/oracle/shot2png.py) and VRAM (vram-V.bin, 1024x512
-- 16-bit, the host's --vram-shot format, tools/vram.py).
--
-- File layout, little endian: "SPYF", version, function address, sample
-- index, vblank, then GPR r0..ra,lo,hi (34 words), CP2D (32), CP2C (32),
-- the scratchpad (1 KB), then for version 1 (.in) main RAM (2 MB); for
-- version 2 (.out) a word count N and N pairs (RAM offset, new value) of
-- the words that differ from the .in.
local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local scratch = PCSX.getScratchPtr()

local out = assert(os.getenv('ORACLE_OUT'), 'ORACLE_OUT not set')
local funcs = os.getenv('ORACLE_FUNCS')
local tracefile = os.getenv('ORACLE_TRACE')
local samples = tonumber(os.getenv('ORACLE_SAMPLES') or '6')
local gap = tonumber(os.getenv('ORACLE_GAP') or '30')
local from = tonumber(os.getenv('ORACLE_FROM') or '0')
local to = tonumber(os.getenv('ORACLE_TO') or '6000')
local padfile = os.getenv('ORACLE_PAD')
local shots, shot_every = {}, tonumber(os.getenv('ORACLE_SHOT_EVERY') or '0')
for v in (os.getenv('ORACLE_SHOTS') or ''):gmatch('%d+') do shots[tonumber(v)] = true end

local vblank = 0

local function u32le(v)
  v = bit.band(v, 0xffffffff)
  return string.char(bit.band(v, 255), bit.band(bit.rshift(v, 8), 255),
    bit.band(bit.rshift(v, 16), 255), bit.rshift(v, 24))
end

local function header(f, version, addr, k)
  local regs = PCSX.getRegisters()
  f:write('SPYF', u32le(version), u32le(addr), u32le(k), u32le(vblank))
  local t = {}
  for i = 0, 33 do t[#t + 1] = u32le(regs.GPR.r[i]) end
  for i = 0, 31 do t[#t + 1] = u32le(regs.CP2D.r[i]) end
  for i = 0, 31 do t[#t + 1] = u32le(regs.CP2C.r[i]) end
  f:write(table.concat(t))
  f:write(ffi.string(scratch, 1024))
end

-- Entry: full RAM; returns a copy to diff the exit against.
local function dump_in(path, addr, k)
  local f = assert(io.open(path, 'wb'))
  header(f, 1, addr, k)
  f:write(ffi.string(mem, 0x200000))
  f:close()
  local copy = ffi.new('uint8_t[?]', 0x200000)
  ffi.copy(copy, mem, 0x200000)
  return copy
end

local function dump_out(path, addr, k, copy)
  local f = assert(io.open(path, 'wb'))
  header(f, 2, addr, k)
  local now, was = ffi.cast('uint32_t*', mem), ffi.cast('uint32_t*', copy)
  local t = {}
  for i = 0, 0x200000 / 4 - 1 do
    if now[i] ~= was[i] then
      t[#t + 1] = u32le(i * 4) .. u32le(now[i])
    end
  end
  f:write(u32le(#t), table.concat(t))
  f:close()
end

-- Pad script, as the host's --pad ("V buttons", vblank then button names),
-- applied as overrides on pad 1. Absolute vblanks are shifted by
-- $ORACLE_PAD_SHIFT (the emulator boots the BIOS first: its title starts
-- 1520 vblanks after the host's). Since load times and dropped frames differ,
-- lines can instead be anchored to the game state: "@gs=0,level=10 +71 up"
-- fires 71 vblanks after gamestate and level first match, counting from when
-- the line before was applied; consecutive lines with the same condition
-- share one anchor.
local pad = {}
local pad_shift = tonumber(os.getenv('ORACLE_PAD_SHIFT') or '0')
if padfile then
  for line in io.lines(padfile) do
    local cond, off, rest = line:match('^%s*@(%S+)%s+%+(%d+)%s+(.*)$')
    if cond then
      local c = {}
      for k, v in cond:gmatch('(%w+)=(%-?%d+)') do c[k] = tonumber(v) end
      pad[#pad + 1] = { cond = cond, c = c, off = tonumber(off), spec = rest }
    else
      local v, r = line:match('^%s*(%d+)%s+(.*)$')
      if v then pad[#pad + 1] = { at = tonumber(v) + pad_shift, spec = r } end
    end
  end
end
local B = PCSX.CONSTS.PAD.BUTTON
local names = { select = B.SELECT, start = B.START, up = B.UP, right = B.RIGHT,
  down = B.DOWN, left = B.LEFT, l2 = B.L2, r2 = B.R2, l1 = B.L1, r1 = B.R1,
  triangle = B.TRIANGLE, circle = B.CIRCLE, cross = B.CROSS, square = B.SQUARE }
local pad1 = PCSX.SIO0.slots[1].pads[1]
local held = {}
local function setpad(spec)
  for b in pairs(held) do pad1.clearOverride(b) end
  held = {}
  for w in spec:lower():gmatch('[%w]+') do
    local b = names[w]
    if b then held[b] = true; pad1.setOverride(b) end
  end
end
local padi, anchor_cond, anchor = 1, nil, nil
local function pad_step(gs, level)
  while padi <= #pad do
    local p = pad[padi]
    if p.at then
      if vblank < p.at then return end
    else
      if p.cond ~= anchor_cond then
        local ok = (p.c.gs == nil or p.c.gs == gs) and (p.c.level == nil or p.c.level == level)
        if not ok then return end
        anchor_cond, anchor = p.cond, vblank
        print(string.format('oracle: pad anchor @%s at vblank %d', p.cond, vblank))
      end
      if vblank < anchor + p.off then return end
    end
    setpad(p.spec)
    padi = padi + 1
  end
end

-- Level ID, for per-level sample budgets ($ORACLE_PER_LEVEL=1: up to
-- $ORACLE_SAMPLES per function in each level, not overall).
local per_level = os.getenv('ORACLE_PER_LEVEL') == '1'
local function level_now() return ffi.cast('int32_t*', mem + 0x7596C)[0] end

-- RAM word for key= and if= ("ADDR:SIZE", PS1 address in hex).
local function ramword(spec)
  local a, size = spec:match('^(%x+):([124])$')
  assert(a, 'bad RAM word ' .. spec)
  local off, ty = bit.band(tonumber(a, 16), 0x1fffff), ({ ['1'] = 'int8_t*', ['2'] = 'int16_t*', ['4'] = 'int32_t*' })[size]
  return function() return ffi.cast(ty, mem + off)[0] end
end

local state = {}
-- Global: PCSX-Redux removes a breakpoint when its handle is garbage collected.
oracleBreakpoints = {}
local bps = oracleBreakpoints
for line in (funcs and io.lines(funcs) or function() end) do
  local a, name, opts = line:match('^%s*(%x+)%s+(%S+)%s*(.*)$')
  if a then
    local addr = tonumber(a, 16)
    local st = { name = name, n = 0, next = from, busy = false, lev = {}, max = samples, gap = gap }
    for k, v in opts:gmatch('(%w+)=(%S+)') do
      if k == 'n' then st.max = tonumber(v)
      elseif k == 'gap' then st.gap = tonumber(v)
      elseif k == 'key' then st.key = ramword(v)
      elseif k == 'if' then
        local w, op, val = v:match('^(%x+:[124])(!?=)(%-?%d+)$')
        local get, want = ramword(w), tonumber(val)
        st.cond = op == '=' and function() return get() == want end or function() return get() ~= want end
      end
    end
    state[#state + 1] = st
    bps[#bps + 1] = PCSX.addBreakpoint(addr, 'Exec', 4, 'oracle ' .. name, function()
      if st.busy or vblank < st.next then return true end
      if st.cond and not st.cond() then return true end
      local lvl = st.key and st.key() or (per_level and level_now() or 0)
      if (st.lev[lvl] or 0) >= st.max then return true end
      local regs = PCSX.getRegisters()
      local sp, ra, k = regs.GPR.n.sp, regs.GPR.n.ra, st.n
      st.busy, st.n, st.next = true, st.n + 1, vblank + st.gap
      st.lev[lvl] = (st.lev[lvl] or 0) + 1
      local copy = dump_in(string.format('%s/%s_%d.in', out, name, k), addr, k)
      local done = false
      st.exit = PCSX.addBreakpoint(ra, 'Exec', 4, 'oracle ret ' .. name, function()
        if done then return false end
        if PCSX.getRegisters().GPR.n.sp ~= sp then return true end
        dump_out(string.format('%s/%s_%d.out', out, name, k), addr, k, copy)
        done, st.busy, copy = true, false, nil
        return false
      end)
      return true
    end)
  end
end

-- Level tour ($ORACLE_TOUR="11,12,..."): once the pad script is done and
-- Spyro is playing, stay $ORACLE_TOUR_STAY vblanks (default 300), then warp
-- to the next level with the game's level-warp cheat: Pause, Down, Down,
-- Cross opens the inventory, where CheatProcessLevelWarp (cheats.c) runs
-- while g_LevelCheatActive; it takes the world digit, then the level digit
-- (CIRCLE 0, CROSS 1, SQUARE 2, TRIANGLE 3, RIGHT 4, DOWN 5).
local tour = {}
for v in (os.getenv('ORACLE_TOUR') or ''):gmatch('%d+') do tour[#tour + 1] = tonumber(v) end
local tour_stay = tonumber(os.getenv('ORACLE_TOUR_STAY') or '300')
-- While staying in a level, $ORACLE_TOUR_PAD ("OFFSET buttons" lines, then
-- "PERIOD loop") plays a pad pattern on repeat, and $ORACLE_TOUR_POKE
-- ("ADDR=VALUE,...", 32-bit words) is written once, 60 vblanks in.
local tour_pad, tour_period = {}, 0
for line in (os.getenv('ORACLE_TOUR_PAD') and io.lines(os.getenv('ORACLE_TOUR_PAD')) or function() end) do
  local off, rest = line:match('^%s*(%d+)%s+(.*)$')
  if off and rest:match('^loop') then tour_period = tonumber(off)
  elseif off then tour_pad[tonumber(off)] = rest end
end
local tour_poke = {}
for a, v in (os.getenv('ORACLE_TOUR_POKE') or ''):gmatch('(%x+)=(%-?%w+)') do
  tour_poke[#tour_poke + 1] = { bit.band(tonumber(a, 16), 0x1fffff), tonumber(v) }
end
local digit = { [0] = 'circle', 'cross', 'square', 'triangle', 'right', 'down' }
local tour_i, tour_t0, tour_seq, tour_at = 1, nil, nil, nil
local function tour_step(gs, level)
  if tour_i > #tour or padi <= #pad then return end
  if tour_seq then
    local t = vblank - tour_at
    local e = tour_seq[t]
    if e == 'cheat' then
      ffi.cast('int32_t*', mem + 0x75880)[0] = 1 -- g_LevelCheatActive
      ffi.cast('int32_t*', mem + 0x758EC)[0] = 0 -- cheat_HomeworldSelected
    elseif e then
      setpad(e)
    end
    if t > 400 then tour_seq = nil end
    return
  end
  if gs ~= 0 then
    if tour_t0 and tour_period > 0 then setpad('none') end
    tour_t0 = nil
    return
  end
  tour_t0 = tour_t0 or vblank
  local t = vblank - tour_t0
  if t == 60 then
    for _, p in ipairs(tour_poke) do ffi.cast('int32_t*', mem + p[1])[0] = p[2] end
  end
  if t < tour_stay then
    if tour_period > 0 and tour_pad[t % tour_period] then setpad(tour_pad[t % tour_period]) end
    return
  end
  if tour_period > 0 then setpad('none') end
  local L = tour[tour_i]
  print(string.format('oracle: tour leaving level %d for %d at vblank %d', level, L, vblank))
  tour_i, tour_t0, tour_at = tour_i + 1, nil, vblank + 1 -- [0] runs next vblank
  tour_seq = { [0] = 'start', [6] = 'none', [60] = 'down', [66] = 'none', [100] = 'down',
    [106] = 'none', [140] = 'cross', [146] = 'none', [200] = 'cheat',
    [220] = digit[math.floor(L / 10)], [226] = 'none', [260] = digit[L % 10], [266] = 'none' }
end

-- psx.map addresses; the rand seed is PsyQ rand()'s D_80075AC0.
local function r32(a) return ffi.cast('int32_t*', mem + bit.band(a, 0x1fffff))[0] end
local trace = tracefile and assert(io.open(tracefile, 'w'))
if trace then
  trace:write('vblank\tgamestate\tlevel\ttick\tx\ty\tz\tstate\thealth\t' ..
    'gems\tdragons\tlives\trand\n')
end
local function trace_row()
  local spyro = 0x80078A58
  trace:write(string.format('%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n',
    vblank, r32(0x800757D8), r32(0x8007596C), r32(0x8007572C), r32(spyro), r32(spyro + 4),
    r32(spyro + 8), r32(spyro + 0x78), r32(spyro + 0x164), r32(0x80075860), r32(0x80075750),
    r32(0x8007582C), bit.tohex(r32(0x80075AC0), 8)))
end

vsyncListener = PCSX.Events.createEventListener('GPU::Vsync', function()
  vblank = vblank + 1
  if trace then trace_row() end
  if shots[vblank] or (shot_every > 0 and vblank % shot_every == 0) then
    local ss = PCSX.GPU.takeScreenShot()
    local f = assert(io.open(string.format('%s/shot-%d.raw', out, vblank), 'wb'))
    f:write(u32le(tonumber(ss.width)), u32le(tonumber(ss.height)), u32le(tonumber(ss.bpp)), tostring(ss.data))
    f:close()
    f = assert(io.open(string.format('%s/vram-%d.bin', out, vblank), 'wb'))
    f:write(tostring(PCSX.GPU.getVRAM()))
    f:close()
  end
  pad_step(r32(0x800757D8), r32(0x8007596C))
  tour_step(r32(0x800757D8), r32(0x8007596C))
  if vblank % 600 == 0 then
    local taken = 0
    for _, st in ipairs(state) do taken = taken + st.n end
    print(string.format('oracle: vblank %d, %d samples', vblank, taken))
  end
  if vblank >= to then
    if trace then trace:close() end
    if not funcs then PCSX.quit(0) return end
    local f = assert(io.open(out .. '/summary.txt', 'w'))
    for _, st in ipairs(state) do f:write(string.format('%s %d\n', st.name, st.n)) end
    f:close()
    PCSX.quit(0)
  end
end)
