
watch("DO", 0x15)
watch("W1", 0xf64969f1)

local doFoo = sensors["DO"][0x15]
local w1Bar = sensors["W1"][0xf64969f1]

-- check if sensors are present

if not doFoo or not w1Bar then
   return false
end

if doFoo.state and w1Bar.value < 47 then
   return false
end

if not doFoo.state and w1Bar.value > 100 then
   return true
end

return doFoo.state
