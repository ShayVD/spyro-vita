-- Behaviour oracle: runs inside PCSX-Redux (porting-plan §7.3, step 13).
-- Loaded with -dofile by tools/oracle/redux; configured by environment:
--   ORACLE_OUT    output directory (default ".")
--   ORACLE_END    vblank to quit at (default 600)
--   ORACLE_SHOTS  comma-separated vblanks to dump VRAM at (vram-<n>.bin)
--   ORACLE_PAD    pad script, same format as the linux32 host's --pad
--                 ("<vblank> <button>[+<button>...]" or "none")
--   ORACLE_TRACE  comma-separated function addresses (hex) to snapshot at
--                 entry and return (tools/oracle/snapshot.py reads them)
--   ORACLE_TRACE_MAX  snapshots per function (default 50)
--   ORACLE_GTE    file of GTE command addresses (tools/oracle/gte_sites.py);
--                 records the GTE registers around each (gte.bin)
--   ORACLE_GTE_MAX / ORACLE_GTE_SITE_MAX  records per op type (default 1000)
--                 and per address (default 100)
-- Vblank 0 is the first GPU vsync after reset, as in the host's count.

local ffi = require('ffi')

local out = os.getenv('ORACLE_OUT') or '.'
local endAt = tonumber(os.getenv('ORACLE_END') or '600')
local traceMax = tonumber(os.getenv('ORACLE_TRACE_MAX') or '50')

local function list(s, base)
    local t = {}
    for v in string.gmatch(s or '', '[^,%s]+') do t[#t + 1] = tonumber(v, base) end
    return t
end

local shots = {}
for _, v in ipairs(list(os.getenv('ORACLE_SHOTS'), 10)) do shots[v] = true end

local B = PCSX.CONSTS.PAD.BUTTON
local names = {
    select = B.SELECT, start = B.START, up = B.UP, right = B.RIGHT, down = B.DOWN, left = B.LEFT,
    l2 = B.L2, r2 = B.R2, l1 = B.L1, r1 = B.R1, triangle = B.TRIANGLE, circle = B.CIRCLE,
    cross = B.CROSS, square = B.SQUARE,
}
local padEvents = {}
local padPath = os.getenv('ORACLE_PAD')
if padPath and padPath ~= '' then
    for line in io.lines(padPath) do
        local n, what = line:match('^%s*(%d+)%s+(%S+)')
        if n then padEvents[tonumber(n)] = what end
    end
end

local pad = PCSX.SIO0.slots[1].pads[1]
local held = {}
local function setPad(what)
    for b in pairs(held) do pad.clearOverride(b) end
    held = {}
    if what == 'none' then return end
    for name in string.gmatch(what, '[^+]+') do
        local b = names[name]
        if b == nil then error('oracle: unknown pad button ' .. name) end
        pad.setOverride(b)
        held[b] = true
    end
end

local t0 = os.time()
local log = io.open(out .. '/oracle.log', 'w')
local function say(fmt, ...)
    local s = string.format(fmt, ...)
    log:write(s, '\n')
    log:flush()
end

local function writeFile(path, ptr, size)
    local f = io.open(path, 'wb')
    f:write(ffi.string(ptr, size))
    f:close()
end

-- VRAM dump (1024x512, 16 bpp, 1 MB) as vram-<n>.bin. PCSX.GPU.takeScreenShot()
-- throws under -no-ui on the 24-bit logo frames, so dump all of VRAM instead;
-- tools/oracle/raw2png.py turns it into a PNG.
local function shoot(n)
    local f = io.open(string.format('%s/vram-%d.bin', out, n), 'wb')
    f:write(tostring(PCSX.GPU.getVRAM()))
    f:close()
end

-- Function snapshots (tools/oracle/oracle.lua -> tests/m2c_diff). Each
-- record in trace-<addr>.bin, little-endian u32 unless noted:
--   'SNP2', kind (0 entry, 1 return), seq, vblank,
--   GPR[34] (r0..ra, lo, hi), pc, GTE data[32], GTE control[32],
--   scratchpad (1024 bytes),
--   nglobals, then per global: PS1 address, size, bytes (ORACLE_GLOBALS),
--   npages, then per page: PS1 address, 4096 bytes.
-- The pages are those of a0..a3 when they point into RAM (and the page after
-- each), and of $sp and the pages either side of it; the return record repeats the
-- entry's page set. The replay maps only these pages, so a function that
-- reads elsewhere shows up as an incomplete capture, not a mismatch.
local MAGIC = 0x32504E53
local regs = PCSX.getRegisters()
local vblank = 0
local traces = {}
OracleTraces = traces
local globals = {}
for spec in string.gmatch(os.getenv('ORACLE_GLOBALS') or '', '[^,%s]+') do
    local a, n = spec:match('^(%x+):(%x+)$')
    globals[#globals + 1] = { tonumber(a, 16), tonumber(n, 16) }
end

local mem = ffi.cast('uint32_t*', PCSX.getMemPtr())
local mem8 = PCSX.getMemPtr()
local function ram32(addr) return mem[bit.rshift(bit.band(addr, 0x1FFFFF), 2)] end

local function u32s(t)
    local b = ffi.new('uint32_t[?]', #t)
    for i = 1, #t do b[i - 1] = t[i] end
    return ffi.string(b, 4 * #t)
end

local function pageSet()
    local set, out = {}, {}
    local function add(a)
        a = tonumber(ffi.cast('uint32_t', a))
        if a < 0x80001000 or a >= 0x80200000 then
            -- Below 0x80001000 is where the port keeps the scratchpad.
            if a >= 0x1000 and a < 0x200000 then a = a + 0x80000000 else return end
        end
        local pg = bit.band(a, 0xFFFFF000)
        pg = tonumber(ffi.cast('uint32_t', pg))
        if not set[pg] then set[pg] = true; out[#out + 1] = pg end
    end
    local n = regs.GPR.n
    for _, r in ipairs({ n.a0, n.a1, n.a2, n.a3, n.sp }) do
        add(r)
        add(r + 0x1000)
    end
    add(n.sp - 0x1000) -- the callee's frame
    return out
end

-- Redux commits a load like the R3000 does, after the next instruction, so at
-- a breakpoint the load in the delay slot just before it (the caller's jal
-- slot at entry, the callee's jr slot at return) is still pending. Returns
-- the GPR index and the value it is about to get, or nil.
local function pendingLoad(slot)
    local w = ram32(slot)
    local op = bit.rshift(w, 26)
    local size = ({ [0x20] = -1, [0x21] = -2, [0x23] = 4, [0x24] = 1, [0x25] = 2 })[op]
    if not size then return nil end
    local rs, rt = bit.band(bit.rshift(w, 21), 31), bit.band(bit.rshift(w, 16), 31)
    local imm = bit.band(w, 0xFFFF)
    if imm >= 0x8000 then imm = imm - 0x10000 end
    local a = tonumber(ffi.cast('uint32_t', regs.GPR.r[rs] + imm))
    local base = (a >= 0x1F800000 and a < 0x1F800400) and PCSX.getScratchPtr() - 0x1F800000 + a
                 or mem8 + bit.band(a, 0x1FFFFF)
    local v
    if size == 4 then v = ffi.cast('uint32_t*', base)[0]
    elseif size == 2 then v = ffi.cast('uint16_t*', base)[0]
    elseif size == -2 then v = ffi.cast('int16_t*', base)[0]
    elseif size == 1 then v = ffi.cast('uint8_t*', base)[0]
    else v = ffi.cast('int8_t*', base)[0] end
    return rt, tonumber(ffi.cast('uint32_t', v))
end

local function snapshot(tr, kind, seq, pages, slot)
    local g = {}
    for i = 0, 33 do g[#g + 1] = regs.GPR.r[i] end
    if slot then
        local rt, v = pendingLoad(slot)
        if rt and rt ~= 0 then g[rt + 1] = v end
    end
    g[#g + 1] = regs.pc
    local cd, cc = {}, {}
    for i = 0, 31 do cd[#cd + 1] = regs.CP2D.r[i]; cc[#cc + 1] = regs.CP2C.r[i] end
    local f = tr.file
    f:write(u32s({ MAGIC, kind, seq, vblank }), u32s(g), u32s(cd), u32s(cc), ffi.string(PCSX.getScratchPtr(), 1024))
    f:write(u32s({ #globals }))
    for _, gl in ipairs(globals) do
        f:write(u32s({ gl[1], gl[2] }), ffi.string(mem8 + bit.band(gl[1], 0x1FFFFF), gl[2]))
    end
    f:write(u32s({ #pages }))
    for _, pg in ipairs(pages) do
        f:write(u32s({ pg }), ffi.string(mem8 + bit.band(pg, 0x1FFFFF), 0x1000))
    end
end

-- ORACLE_TRACE entries are ADDR or ADDR-END (hex); with an end, the
-- function's `jr $ra` sites get breakpoints so the return knows which delay
-- slot ran.
for spec in string.gmatch(os.getenv('ORACLE_TRACE') or '', '[^,%s]+') do
    local addr, fend = spec:match('^(%x+)%-?(%x*)$')
    addr, fend = tonumber(addr, 16), tonumber(fend, 16)
    local tr = { addr = addr, count = 0, bps = {} }
    tr.file = io.open(string.format('%s/trace-%08X.bin', out, addr), 'wb')
    traces[#traces + 1] = tr
    tr.entry = PCSX.addBreakpoint(addr, 'Exec', 4, 'oracle entry', function()
        if fend and not tr.scanned then
            -- The code is only in RAM once the game has loaded.
            tr.scanned = true
            for a = addr, fend - 4, 4 do
                if ram32(a) == 0x03E00008 and bit.rshift(ram32(a + 4), 26) >= 0x20 and
                   bit.rshift(ram32(a + 4), 26) <= 0x25 then
                    tr.bps[#tr.bps + 1] = PCSX.addBreakpoint(a, 'Exec', 4, 'oracle exit', function()
                        tr.exit = a
                        return true
                    end)
                end
            end
        end
        if tr.count >= traceMax then
            tr.entry:disable()
            return true
        end
        tr.count = tr.count + 1
        local seq, ra, sp = tr.count, regs.GPR.n.ra, regs.GPR.n.sp
        local pages = pageSet()
        snapshot(tr, 0, seq, pages, ra - 4)
        tr.exit = nil
        -- The return: the first time the caller's $ra executes with $sp back
        -- where it was at entry.
        local bp, done
        bp = PCSX.addBreakpoint(ra, 'Exec', 4, 'oracle return', function()
            -- Removal waits for nextTick, so later calls returning here must
            -- not write another return under this seq.
            if done or regs.GPR.n.sp ~= sp then return true end
            done = true
            snapshot(tr, 1, seq, pages, tr.exit and tr.exit + 4)
            bp:disable()
            PCSX.nextTick(function() bp:remove() end)
            return true
        end)
        tr.bps[#tr.bps + 1] = bp
        return true
    end)
end

-- GTE vectors (step 8). Each record in gte.bin is 131 u32: address,
-- instruction word, vblank, then data[32] control[32] before the command and
-- data[32] control[32] after it (raw register storage).
local gteSites = 0
OracleGte = {}
local gtePath = os.getenv('ORACLE_GTE')
if gtePath and gtePath ~= '' then
    local opMax = tonumber(os.getenv('ORACLE_GTE_MAX') or '1000')
    local siteMax = tonumber(os.getenv('ORACLE_GTE_SITE_MAX') or '100')
    local gf = io.open(out .. '/gte.bin', 'wb')
    OracleGte.file = gf
    local perOp, perSite, pending = {}, {}, {}
    local rec = ffi.new('uint32_t[131]')
    -- Redux commits mtc2/ctc2/lwc2 like a delayed load: at a breakpoint on
    -- the GTE command, a move one or two instructions earlier has not reached
    -- the register file yet, though the command will see it. Replay those
    -- into the "before" state (psx-spx MTC2/CTC2 side effects included).
    local function lzc(v)
        if bit.band(v, 0x80000000) ~= 0 then v = bit.bnot(v) end
        local n = 0
        while n < 32 and bit.band(v, 0x80000000) == 0 do v = bit.lshift(v, 1); n = n + 1 end
        return n
    end
    function pendingCop2(p, addr)
        for _, back in ipairs({ 8, 4 }) do
            local w = ram32(addr - back)
            local op, rs = bit.rshift(w, 26), bit.band(bit.rshift(w, 21), 31)
            local rt, rd = bit.band(bit.rshift(w, 16), 31), bit.band(bit.rshift(w, 11), 31)
            local dreg, creg, v
            if op == 0x12 and rs == 4 then
                dreg, v = rd, regs.GPR.r[rt]
            elseif op == 0x12 and rs == 6 then
                creg, v = rd, regs.GPR.r[rt]
            elseif op == 0x32 then
                local imm = bit.band(w, 0xFFFF)
                if imm >= 0x8000 then imm = imm - 0x10000 end
                dreg, v = rt, ram32(regs.GPR.r[rs] + imm)
            end
            v = v and tonumber(ffi.cast('uint32_t', v))
            if dreg == 15 then
                p[12], p[13], p[14], p[15] = p[13], p[14], v, v
            elseif dreg == 28 then
                p[9], p[10], p[11], p[28] = bit.lshift(bit.band(v, 0x1F), 7), bit.lshift(bit.band(v, 0x3E0), 2),
                                            bit.rshift(bit.band(v, 0x7C00), 3), v
            elseif dreg == 30 then
                p[30], p[31] = v, lzc(v)
            elseif dreg and dreg ~= 31 then
                p[dreg] = v
            elseif creg then
                p[32 + creg] = v
            end
        end
    end
    for line in io.lines(gtePath) do
        local addr = tonumber(line, 16)
        if addr then
            gteSites = gteSites + 1
            perSite[addr] = 0
            local before, after
            before = PCSX.addBreakpoint(addr, 'Exec', 4, 'oracle gte', function()
                local w = ram32(addr)
                local op = bit.band(w, 0x3F)
                if bit.band(w, 0xFE000000) ~= 0x4A000000 then return true end
                if (perOp[op] or 0) >= opMax or perSite[addr] >= siteMax then
                    -- Hot sites run thousands of times a frame: stop paying for them.
                    before:disable()
                    after:disable()
                    return true
                end
                local p = pending[addr] or ffi.new('uint32_t[64]')
                for i = 0, 31 do p[i] = regs.CP2D.r[i]; p[32 + i] = regs.CP2C.r[i] end
                pendingCop2(p, addr)
                pending[addr] = p
                pending[addr + 4] = addr
                return true
            end)
            after = PCSX.addBreakpoint(addr + 4, 'Exec', 4, 'oracle gte after', function()
                local site = pending[addr + 4]
                if site ~= addr then return true end
                pending[addr + 4] = nil
                local w = ram32(addr)
                local op = bit.band(w, 0x3F)
                perOp[op] = (perOp[op] or 0) + 1
                perSite[addr] = perSite[addr] + 1
                rec[0], rec[1], rec[2] = addr, w, vblank
                ffi.copy(rec + 3, pending[addr], 256)
                for i = 0, 31 do rec[67 + i] = regs.CP2D.r[i]; rec[99 + i] = regs.CP2C.r[i] end
                gf:write(ffi.string(rec, 524))
                return true
            end)
            OracleGte[#OracleGte + 1] = before
            OracleGte[#OracleGte + 1] = after
        end
    end
    OracleGte.perOp = perOp
end

say('oracle: end=%d trace=%d functions, gte=%d sites', endAt, #traces, gteSites)

-- Global: a listener (or breakpoint) only held by a chunk local is garbage
-- once this file has run, and the next big allocation collects it.
OracleVsync = PCSX.Events.createEventListener('GPU::Vsync', function()
    local what = padEvents[vblank]
    if what then setPad(what) end
    if shots[vblank] then shoot(vblank) end
    if vblank % 60 == 0 then
        say('vblank %d t %d pc %08X gamestate %d level %d unprocessed %d', vblank, os.time() - t0, regs.pc, ram32(0x800757D8),
            ram32(0x8007596C), ram32(0x80075760))
    end
    if vblank >= endAt then
        for _, tr in ipairs(traces) do
            say('trace %08X: %d snapshots', tr.addr, tr.count)
            tr.file:close()
        end
        if OracleGte.file then
            OracleGte.file:close()
            for op, n in pairs(OracleGte.perOp) do say('gte op %02X: %d records', op, n) end
        end
        say('oracle: quit at vblank %d', vblank)
        log:close()
        PCSX.quit(0)
    end
    vblank = vblank + 1
end)
