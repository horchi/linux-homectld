-- Danfoss Klemme D Blinkcode Erkennung
-- Pin D invertiert: 1 = Impuls aktiv, 0 = Ruhezustand
-- Voraussetzung: sensorDI_12 und signal als Lua-Globals gesetzt (via pushGlobal)
--
-- signal == true  -> Interrupt-Aufruf (Flanke erkannt)
-- signal == false -> Zyklischer Aufruf (~1 Minute)

pulseCount    = pulseCount    or 0
lastPulseTime = lastPulseTime or 0
blinkCode     = blinkCode     or 0
lastValue     = lastValue     or false

local SEQ_PAUSE_S   = 2
local RESET_PAUSE_S = 10
local now   = os.time()
local value = sensorDI_12

if signal then
   if value and not lastValue then
      if lastPulseTime > 0 and (now - lastPulseTime) >= SEQ_PAUSE_S then
         blinkCode  = pulseCount
         pulseCount = 1
      else
         pulseCount = pulseCount + 1
      end
      lastPulseTime = now
   end
   lastValue = value
else
   if pulseCount > 0 and lastPulseTime > 0 and (now - lastPulseTime) >= SEQ_PAUSE_S then
      blinkCode  = pulseCount
      pulseCount = 0
   end
   if blinkCode > 0 and lastPulseTime > 0 and (now - lastPulseTime) >= RESET_PAUSE_S then
      blinkCode     = 0
      pulseCount    = 0
      lastPulseTime = 0
   end
end

local codes = {
   [0] = "Kein Fehler",
   [1] = "Batteriestrom Abschaltung",
   [2] = "Lüfter Überstrom",
   [3] = "Startfehler",
   [4] = "Mindestdrehzahl-Fehler",
   [5] = "Thermische Abschaltung"
}

return codes[blinkCode] or ("Unbekannter Code: " .. blinkCode)
