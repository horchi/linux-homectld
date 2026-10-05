-- Hysterese mit Mindesthaltezeit
--   true  sobald level > 100, für genau 1 Minute
--   danach false, Wiederauslösung erst wenn level < 50 war und erneut > 100 steigt

watch("AI", 0x01)
every(10)

if sensors["AI"][0x01] == nil or not sensors["AI"][0x01].valid then
   return false
end

local level = sensors["AI"][0x01].value

-- first run (daemon start): a full tank must not raise the warning, start in 'cooldown'

if state == nil then
   state       = level > 100 and "cooldown" or "armed"
   activeSince = 0
end

local now = os.time()
local result = false

if state == "armed" then

   if level > 100 then
      state       = "active"
      activeSince = now
      result      = true
   end

elseif state == "active" then

   if (now - activeSince) >= 60 then
      state = "cooldown"
   else
      result = true
   end

elseif state == "cooldown" then

   if level < 50 then
      state = "armed"
   end

end

return result

