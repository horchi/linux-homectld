-- calculate PW work in [AH] of today

-- between 00:00 and 01:00 -> reset

local nowMin = os.date("*t", os.time()).hour * 60 + os.date("*t", os.time()).min

if nowMin < 1 * 60 then
   return 0.00
end

watch("AI", 0x08)

local cv1 = sensors["CV"][0x01]   -- old value of PV work
local ai8 = sensors["AI"][0x08]   -- actual PV current

tell(eloDebug, "PV-Work: Calculating ..")

-- already initialized?

if not ai8 then
   tell(eloDetail, "PV-Work: About AI08 not initialzed")
   return 0
end

local lastCalc = 0
local lastWork = 0

if cv1 then
   lastCalc = cv1.last
   lastWork = cv1.value
end

if lastCalc == 0 or ai8.last == 0 then
   return lastWork
end

tell(eloDebug, "PV-Work: was " .. sensors["CV"][0x01].value .. " now " .. lastWork + (ai8.value * ((os.time()-lastCalc) / 3600.0)))

return lastWork + (ai8.value * ((os.time()-lastCalc) / 3600.0))
