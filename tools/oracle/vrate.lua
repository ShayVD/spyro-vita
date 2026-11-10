-- The video rate PCSX-Redux runs a disc at (regions plan §8, D132): emulated
-- CPU cycles between vblanks. The CPU runs at 33,868,800 Hz, so NTSC
-- (59.94 Hz) is about 565,000 cycles a vblank and PAL (50 Hz) about 677,000.
-- Prints one line every ORACLE_EVERY vblanks (default 300) and a summary at
-- ORACLE_FRAMES (default 1200), then quits. The summary's rate is the last
-- window's: with the US BIOS (SCPH-1001) Redux starts every disc at NTSC and
-- switches when the game sets its video mode (the PAL disc's
-- GraphicsInitialize, about vblank 1,400). ORACLE_WORDS: hex addresses whose
-- RAM word is printed with each line (a region's `main`, say, to see the
-- game is running). Run with tools/oracle/run.sh (DISC= for the image).
local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local function r32(a) return ffi.cast('uint32_t*', mem + bit.band(a, 0x1fffff))[0] end
local CPU_HZ = 33868800
local every = tonumber(os.getenv('ORACLE_EVERY') or '300')
local frames = tonumber(os.getenv('ORACLE_FRAMES') or '1200')
local words = {}
for a in (os.getenv('ORACLE_WORDS') or ''):gmatch('%x+') do words[#words + 1] = tonumber(a, 16) end
local n, last, from = 0, nil, nil
local function hz(cycles, vblanks) return CPU_HZ * vblanks / cycles end
vsyncListener = PCSX.Events.createEventListener('GPU::Vsync', function()
  local c = tonumber(PCSX.getCPUCycles())
  n = n + 1
  if n % every == 0 then
    local w = {}
    for _, a in ipairs(words) do w[#w + 1] = string.format('%08x=%08x', a, r32(a)) end
    local span = last and (c - last) or 0
    print(string.format('vrate: vblank %d cycles %d (%.0f a vblank, %.3f Hz) %s', n, c,
      span / every, span > 0 and hz(span, every) or 0, table.concat(w, ' ')))
    from, last = last, c
  end
  if n >= frames then
    local per = (c - from) / every
    print(string.format('vrate: vblanks %d..%d: %.1f cycles a vblank, %.3f Hz: %s', n - every, n,
      per, hz(c - from, every), per > 621000 and 'PAL (50 Hz)' or 'NTSC (60 Hz)'))
    PCSX.quit(0)
  end
end)
