-- Calculates and increments the compressor's daily runtime live in minutes during execution.

-- It's important to persist the sensor data (in database)!

-- 1. Register sensor subscription event listeners

watch("GPIO", 0x0c)

-- 2. Global state initialization

local compressor = sensors["GPIO"][0x0c]    -- Danfoss F-Signal
local lastRuntime = sensors["CV"][0x06]     -- #TODO set a valiable like myAddress prior calling the script

if not compressor then
   tell(eloDetail, "Comp-Time: GPIO:0x0c not initialized")
   return 0
end

if not lastRuntime then
   tell(eloDetail, "Comp-Time: CV:0x06 not initialized")
   return 0
end

local dailyRuntimeSeconds = lastRuntime.value * 60
local nowMin = os.date("*t", os.time()).hour * 60 + os.date("*t", os.time()).min

-- 2. Daily reset execution between 00:00 and 01:00 AM

if nowMin < 1 * 60 then
   dailyRuntimeSeconds = 0
   lastCheckTime = nil
   return 0.00
end

local currentTime = os.time()
local isRunning = compressor.state

-- 4. Dynamic time delta calculation for both live cycles and edge interrupts

if isRunning then
   if lastCheckTime and lastCheckTime < currentTime then
      -- Calculate and add elapsed seconds since last cycle/edge immediately
      local deltaSeconds = currentTime - lastCheckTime
      dailyRuntimeSeconds = dailyRuntimeSeconds + deltaSeconds
      tell(eloDebug, "Comp-Time: Compressor is running " .. dailyRuntimeSeconds .. "seconds now")
   end
   -- Store current timestamp to continue tracking from this point
   lastCheckTime = currentTime
else
   -- Reset tracking anchor when compressor is idle to prevent cross-cycle math errors
   lastCheckTime = nil
end

-- 5. Return live accumulated daily runtime formatted into minutes

tell(eloDebug, "Comp-Time: Compressor time today now " .. math.floor((dailyRuntimeSeconds / 60.0) * 100 + 0.5) / 100 .. " minutes")

return math.floor((dailyRuntimeSeconds / 60.0) * 100 + 0.5) / 100
