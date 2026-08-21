
watch("CV", 0x01)
watch("DS18", 0x60f3ee42)
watch("GPIO", 0x0b) -- Monitor filter pump for immediate reaction on state change

-- Read sensor values

local delta = sensors["CV"][0x01]
local pool = sensors["DS18"][0x60f3ee42]
local filter = sensors["GPIO"][0x0b]

if not delta or not pool or not filter then
   return false
end

-- Determine filter pump state (state == true means pump is running)

local filterPumpOn = filter.valid and (filter.state == true)

----------------------------------------------------
-- 1. Runtime & Status Calculation via changedAt
----------------------------------------------------

local currentTime = os.time()
local sinceMinutes = 0

if filter.changedAt then
   sinceMinutes = math.floor((currentTime - filter.changedAt) / 60)
end

-- Read minimum runtime in minutes from config and convert to seconds (Fallback: 15 minutes)

local enableSolarAfterMinutes = config["enableSolarAfter"] or 15
local enableSolarAfterSeconds = enableSolarAfterMinutes * 60

-- Check if filter pump IS RUNNING and has been running long enough

local filterHasRunLongEnough = filterPumpOn and filter.changedAt and ((currentTime - filter.changedAt) >= enableSolarAfterSeconds)

----------------------------------------------------
-- 2. Solar Pump Logic
----------------------------------------------------

-- Startup initialization

if gSolarStatus == nil then
   gSolarStatus = false
end

-- If filter pump has not been running long enough, turn OFF immediately

if not filterHasRunLongEnough then
   gSolarStatus = false
elseif pool.value >= config["tPoolMax"] then
   -- Pool already warm enough
   gSolarStatus = false
else
   -- Regular hysteresis logic (only active if filter runs long enough & pool not too warm)
   if gSolarStatus then
      -- Remains ON until OFF threshold is reached
      gSolarStatus = (delta.value > config["tSolarOff"])
   else
      -- Turns ON only after ON threshold is reached
      gSolarStatus = (delta.value > config["tSolarOn"])
   end
end

----------------------------------------------------
-- 3. Telemetry / Logging & Return
----------------------------------------------------

tell(eloAlways, "LUA (DO:0x0c): Temp Delta: " .. delta.value .. " | Pool Temp: " .. pool.value .. " | Filter Pump: " .. tostring(filterPumpOn) .. " since " .. sinceMinutes .. " min | Solar Pump: " .. tostring(gSolarStatus))

return gSolarStatus
