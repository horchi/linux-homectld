-- Daily runtime of the compressor (Danfoss / Secop F-signal on a GPIO input) in minutes.
--
-- Sensor: CV (calculated value), unit 'min'. The seconds are counted in the script itself
-- (globals survive between the runs), only the result is rounded to minutes. The runtime is
-- also persisted by the daemon as the sensor value, so after a restart of the daemon the
-- stored minutes are taken over as the starting point of the day.
-- Note: all scripts share one Lua state, so the globals (runtimeSeconds, dayOfCount,
-- lastCheckTime) must not be used by another script - give them unique names if in doubt.

local gpioType, gpioAddr = "GPIO", 0x0c          -- the F-signal input (true = compressor running)
local myType, myAddr     = "CV", 0x06            -- this sensor

watch(gpioType, gpioAddr)                        -- run at once on every change of the input
every(10)                                        -- and at least every 10 seconds while it runs

local compressor = sensors[gpioType][gpioAddr]
local me         = sensors[myType][myAddr]

if not compressor then
   tell(eloDetail, "Comp-Time: " .. gpioType .. ":" .. gpioAddr .. " not initialized")
   return 0
end

local now   = os.time()
local today = os.date("*t", now).yday

-- first run after a start of the daemon: continue with the stored minutes of today

if runtimeSeconds == nil then
   runtimeSeconds = (me and me.value or 0) * 60
   dayOfCount     = today
   lastCheckTime  = nil
end

-- a new day starts at 0

if dayOfCount ~= today then
   runtimeSeconds = 0
   dayOfCount     = today
   lastCheckTime  = nil
end

-- add the time since the last run while the compressor is running

if compressor.state then
   if lastCheckTime then
      runtimeSeconds = runtimeSeconds + (now - lastCheckTime)
   end
   lastCheckTime = now
else
   lastCheckTime = nil
end

tell(eloDebug, string.format("Comp-Time: %d seconds today, compressor %s", runtimeSeconds, compressor.state and "running" or "off"))

return math.floor(runtimeSeconds / 60 + 0.5)
