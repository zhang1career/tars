-- mux_pin_walk: hotplug tars-io-mux v0.1a and chase PB2/PB3/PB4 (~500 ms each).
--
-- A0/A1/A2 are binary mux selects. One-hot chase uses channels 1, 2, 4 only
-- (PB2 / PB3 / PB4). Stops when the node disappears; resumes on re-insert.
--
--   python3 tools/tars-pack.py lua tools/examples/mux_pin_walk.lua \
--     -o installs/mux_pin_walk.tlua --name mux_pin_walk --timeslice 1 --priority 20
--   python3 tools/tars-send.py installs/mux_pin_walk.tlua -p /dev/tty.usbmodem*
--   io log cdc
--   app submit mux_pin_walk

local PID_IO_MUX_HV = 0x0002  -- tars-io-mux v0.1a (ADG1408)
local VENDOR_TARS = 0x5441
local DWELL_SLICES = 5        -- 5 x 100 ms timeslice ≈ 500 ms
local RESCAN_SLICES = 5       -- retry discover every ~500 ms when absent
-- One-hot on A2..A0: ch1=001 (PB2), ch2=010 (PB3), ch4=100 (PB4)
local CHASE = { 1, 2, 4 }

local function find_io_mux_v01a()
  local n = tars.nodebus_scan()
  if n == nil or n <= 0 then
    return nil
  end
  local count = tars.nodebus_count()
  local i = 0
  while i < count do
    local node = tars.nodebus_get(i)
    if node and node.present and (not node.conflict)
        and node.vendor_id == VENDOR_TARS
        and node.product_id == PID_IO_MUX_HV then
      return node.addr, node.fw_ver
    end
    i = i + 1
  end
  return nil
end

local addr = nil
local fw = 0
local step = 1
local dwell = 0
local wait = 0

tars.log("mux_pin_walk: wait for tars-io-mux v0.1a (pid=0x0002)")

while true do
  if addr == nil then
    if wait == 0 then
      local a, v = find_io_mux_v01a()
      if a ~= nil then
        addr = a
        fw = v or 0
        step = 1
        dwell = 0
        tars.log(string.format(
          "mux_pin_walk: online 0x%02X fw=%d.%d chase PB2/PB3/PB4",
          addr, math.floor(fw / 256), fw % 256))
      end
    end
    wait = wait + 1
    if wait >= RESCAN_SLICES then wait = 0 end
  else
    if dwell == 0 then
      local ch = CHASE[step]
      local st = tars.nodebus_mux(addr, ch)
      if st ~= 0 then
        tars.log(string.format(
          "mux_pin_walk: offline (mux 0x%02X ch=%d err=%d)", addr, ch, st))
        addr = nil
        wait = 0
      else
        tars.log(string.format("mux_pin_walk: 0x%02X ch=%d", addr, ch))
        step = step + 1
        if step > #CHASE then step = 1 end
      end
    end
    dwell = dwell + 1
    if dwell >= DWELL_SLICES then dwell = 0 end
  end
  tars.yield()
end
