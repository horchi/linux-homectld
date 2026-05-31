-- Danfoss Klemme D Blinkcode Erkennung
-- Pin D invertiert: 1 = Impuls aktiv, 0 = Ruhezustand
--
-- signal == true  -> Interrupt-Aufruf (Flanke erkannt)
-- signal == false -> Zyklischer Aufruf (~1 Minute)

pulseCount    = pulseCount    or 0
lastPulseTime = lastPulseTime or 0
blinkCode     = blinkCode     or 0
lastValue     = lastValue     or false

watch("GPIO", 0x0b)

local SEQ_PAUSE_S = 2
local RESET_PAUSE_S = 10
local now = os.time()
local diagnoseInput = sensors["GPIO"][0x0b]

if not diagnoseInput then
   return "unknown"
end

-- signal ist set by homectld as descibed above

if signal then
   if diagnoseInput.state and not lastValue then
      if lastPulseTime > 0 and (now - lastPulseTime) >= SEQ_PAUSE_S then
         blinkCode  = pulseCount
         pulseCount = 1
      else
         pulseCount = pulseCount + 1
      end
      lastPulseTime = now
   end
   lastValue = diagnoseInput.state
else
   if pulseCount > 0 and lastPulseTime > 0 and (now - lastPulseTime) >= SEQ_PAUSE_S then
      blinkCode  = pulseCount
      pulseCount = 0
   end
   if blinkCode > 0 and lastPulseTime > 0 and (now - lastPulseTime) >= RESET_PAUSE_S then
      blinkCode = 0
      pulseCount = 0
      lastPulseTime = 0
   end
end

local codes = {
   [0] = "Online",
   [1] = "Low Battery",
   [2] = "Fan Overcurrent",
   [3] = "Start Failure",
   [4] = "RPM Limit",
   [5] = "Overheating"
}

-- not used yet:

local mdi_icons = {
   [0] = "mdi:mdi-check-decagram",       -- Everything OK
   [1] = "mdi:mdi-battery-alert",        -- Voltage cut
   [2] = "mdi:mdi-fan-alert",            -- Fan overcurrent
   [3] = "mdi:mdi-engine-off",           -- Compressor won't start
   [4] = "mdi:mdi-speedometer-slow",     -- RPM below limit
   [5] = "mdi:mdi-thermometer-alert"     -- Overheating electronic
}

return codes[blinkCode] or ("Error " .. blinkCode)
