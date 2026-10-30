-- Smoke test for the PCSX-Redux oracle: count vblanks, print a few game
-- variables (psx.map addresses), quit. Run with tools/oracle/run.sh.
local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local function r32(a) return ffi.cast('int32_t*', mem + bit.band(a, 0x1fffff))[0] end
local n = 0
vsyncListener = PCSX.Events.createEventListener('GPU::Vsync', function()
  n = n + 1
  if n % 120 == 0 then
    print(string.format('vblank %d gamestate %d level %d spyro z %d', n,
      r32(0x800757D8), r32(0x8007596C), r32(0x80078A58 + 8)))
  end
  if n >= 1200 then PCSX.quit(0) end
end)
