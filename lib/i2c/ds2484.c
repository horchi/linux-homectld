//***************************************************************************
// DS2484 I2C-to-1-Wire Master Interface
// File ds2484.cpp
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2026-2026 - Jörg Wendel
//***************************************************************************

#include "ds2484.h"

//***************************************************************************
// Ds2484 Konstruktoren
//***************************************************************************

Ds2484::Ds2484(const Ds2484& source)
   : I2C{source}
{
}

Ds2484::Ds2484(Ds2484&& source)
   : I2C{std::move(source)}
{
}

//***************************************************************************
// Init
//***************************************************************************

int Ds2484::init(const char* aDevice, uint8_t aAddress, uint8_t aTcaAddress)
{
   int status {I2C::init(aDevice, aAddress, aTcaAddress)};

   if (status != success)
      return status;

   // Hardware-Reset der Bridge durchführen
   if (deviceReset() != success)
   {
      tell(eloAlways, "Error: DS2484 Hardware Reset failed during init");
      return fail;
   }

   // Standardkonfiguration setzen: Active Pullup aktivieren (empfohlen für stabile Pegel)
   uint8_t configByte = cfgAPU;
   if (writeConfig(configByte) != success)
   {
      tell(eloAlways, "Error: DS2484 Configuration failed during init");
      return fail;
   }

   tell(eloInfo, "DS2484 initialized successfully on address 0x%02X", aAddress);
   return success;
}

//***************************************************************************
// Device Reset
//***************************************************************************

int Ds2484::deviceReset()
{
   uint8_t cmd = cmdDeviceReset;

   if (::write(fd, &cmd, 1) != 1)
   {
      tell(eloAlways, "Error: Sending Device Reset command failed");
      return fail;
   }

   // DS2484 sendet nach einem Reset direkt das geänderte Statusregister zurück
   uint8_t status {0};
   if (::read(fd, &status, 1) != 1)
   {
      tell(eloAlways, "Error: Reading status after Device Reset failed");
      return fail;
   }

   // Bit RST (0x10) muss nach einem Hardware-Reset gesetzt sein
   if (!(status & statusRST))
   {
      tell(eloDetail, "Warning: DS2484 RST-Bit not set after reset (Status: 0x%02X)", status);
   }

   return success;
}

//***************************************************************************
// Write Config
//***************************************************************************

int Ds2484::writeConfig(uint8_t config)
{
   uint8_t buf[2] {};
   buf[0] = cmdWriteDeviceConfig;
   // Das Datenblatt verlangt das Konfigurations-Nibble im High-Byte
   // und das invertierte Konfigurations-Nibble im Low-Byte: (N3..N0) | (~N3..~N0)
   buf[1] = (config & 0x0F) | ((~config << 4) & 0xF0);

   if (::write(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: Writing configuration register failed");
      return fail;
   }

   // Bestätigungsbyte vom DS2484 auslesen (gibt die aktive Konfiguration zurück)
   uint8_t verify {0};
   if (::read(fd, &verify, 1) != 1)
   {
      tell(eloAlways, "Error: Verification read for configuration failed");
      return fail;
   }

   if (verify != (config & 0x0F))
   {
      tell(eloAlways, "Error: Configuration verification mismatch. Expected: 0x%02X, Got: 0x%02X", config, verify);
      return fail;
   }

   return success;
}

//***************************************************************************
// Read Status
//***************************************************************************

int Ds2484::readStatus(uint8_t& status)
{
   // Ein reiner Lesezugriff auf das geöffnete I2C-Device (ohne vorherigen Schreibbefehl)
   // liefert beim DS2484 immer das aktuelle Statusregister zurück.
   if (::read(fd, &status, 1) != 1)
   {
      tell(eloDebug, "Error: Reading status register directly failed");
      return fail;
   }
   return success;
}

//***************************************************************************
// Wait On Busy (Hilfsfunktion)
//***************************************************************************

int Ds2484::waitOnBusy(uint8_t& status, int timeoutMs)
{
   time_t timeoutAt = time(0) + (timeoutMs / 1000 == 0 ? 1 : timeoutMs / 1000);

   do {
      if (readStatus(status) != success)
      {
         return fail;
      }

      if (!(status & status1WB))
      {
         return success; // 1-Wire Bus frei, Operation beendet
      }

      usleep(100); // Kurze Pause zum Entlasten der CPU

      if (time(0) > timeoutAt)
      {
         tell(eloAlways, "Error: Timeout waiting for 1-Wire Bus to become free");
         return fail;
      }
   } while (true);
}

//***************************************************************************
// 1-Wire Bus Reset
//***************************************************************************

int Ds2484::wireReset(bool& presenceDetected)
{
   uint8_t buf[2] {};
   buf[0] = cmd1WireReset;
   buf[1] = 0x4B; // Datenblatt-spezifischer Parameterwert für den Reset-Befehl

   presenceDetected = false;

   if (::write(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: Sending 1-Wire Reset command failed");
      return fail;
   }

   uint8_t status {0};
   if (waitOnBusy(status) != success)
   {
      return fail;
   }

   // Prüfen, ob ein Kurzschluss vorliegt
   if (status & statusSD)
   {
      tell(eloAlways, "Error: 1-Wire Short Circuit detected!");
      return fail;
   }

   // Vorhandensein eines 1-Wire Slaves (Presence Pulse) prüfen
   presenceDetected = (status & statusPPD);

   return success;
}

//***************************************************************************
// 1-Wire Write Byte
//***************************************************************************

int Ds2484::wireWriteByte(uint8_t byte)
{
   uint8_t buf[2] {};
   buf[0] = cmd1WireWriteByte;
   buf[1] = byte;

   uint8_t status {0};
   // Absicherung vor dem Senden: Bus muss frei sein
   if (waitOnBusy(status) != success) return fail;

   if (::write(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: 1-Wire Write Byte failed");
      return fail;
   }

   // Warten bis das Byte fertig auf die 1-Wire-Leitung moduliert wurde
   return waitOnBusy(status);
}

//***************************************************************************
// 1-Wire Read Byte
//***************************************************************************

int Ds2484::wireReadByte(uint8_t& byte)
{
   uint8_t cmd = cmd1WireReadByte;
   uint8_t status {0};

   if (waitOnBusy(status) != success) return fail;

   if (::write(fd, &cmd, 1) != 1)
   {
      tell(eloAlways, "Error: Sending 1-Wire Read Byte command failed");
      return fail;
   }

   if (waitOnBusy(status) != success) return fail;

   // Nach Abschluss des Read-Befehls muss der I2C-Pointer auf das Read-Data Register
   // gerichtet werden. Das geschieht implizit, wir lesen nun das empfangene Datenbyte.
   if (::read(fd, &byte, 1) != 1)
   {
      tell(eloAlways, "Error: Fetching 1-Wire Read Byte data failed");
      return fail;
   }

   return success;
}

//***************************************************************************
// 1-Wire Write Bit
//***************************************************************************

int Ds2484::wireWriteBit(bool bit)
{
   uint8_t cmd = cmd1WireSingleBit;
   // Bit 7 definiert den zu schreibenden Wert (0x80 für 1, 0x00 für 0)
   if (bit) cmd |= 0x80;

   uint8_t status {0};
   if (waitOnBusy(status) != success) return fail;

   if (::write(fd, &cmd, 1) != 1)
   {
      tell(eloAlways, "Error: 1-Wire Write Bit failed");
      return fail;
   }

   return waitOnBusy(status);
}

//***************************************************************************
// 1-Wire Read Bit
//***************************************************************************

int Ds2484::wireReadBit(bool& bit)
{
   uint8_t cmd = cmd1WireSingleBit | 0x80; // Für Read muss Bit 7 auf 1 gesetzt sein
   uint8_t status {0};

   if (waitOnBusy(status) != success) return fail;

   if (::write(fd, &cmd, 1) != 1)
   {
      tell(eloAlways, "Error: 1-Wire Read Bit command failed");
      return fail;
   }

   if (waitOnBusy(status) != success) return fail;

   // Das gelesene Bit befindet sich im SBR-Bit (0x20) des zurückgegebenen Status
   bit = (status & statusSBR) != 0;

   return success;
}

//***************************************************************************
// 1-Wire Triplet (Für ROM-Suchalgorithmus / Search ROM)
//***************************************************************************

int Ds2484::wireTriplet(uint8_t searchDirection, uint8_t& status)
{
   uint8_t cmd = cmd1WireTriplet;
   // Bit 7 definiert die eingeschlagene Richtung bei einem Konflikt
   if (searchDirection) cmd |= 0x80;

   if (waitOnBusy(status) != success) return fail;

   if (::write(fd, &cmd, 1) != 1)
   {
      tell(eloAlways, "Error: 1-Wire Triplet command failed");
      return fail;
   }

   // Das Statusregister enthält nach dem Triplet-Befehl wichtige
   // Weiterschalt-Informationen für das Suchverfahren (TSB und DIR Bits)
   return waitOnBusy(status);
}

//***************************************************************************
// Search ROM (Finds all connected 1-Wire devices on the bus)
//***************************************************************************

int Ds2484::searchRom(SensorList& foundSensors)
{
   uint8_t romId[8] {};
   int lastDiscrepancy {0};
   bool done {false};

   foundSensors.clear();

   // Schleife läuft, bis alle Abzweigungen im Suchbaum abgearbeitet sind
   while (!done)
   {
      bool presence {false};

      if (wireReset(presence) != success)
         return fail;

      if (!presence)
         return success; // Keine Devices am Bus vorhanden

      // Sende den standardmäßigen Search-ROM Befehl (0xF0) an alle Slaves
      if (wireWriteByte(0xF0) != success)
         return fail;

      int currentBit {1};
      int discrepancyMarker {0};

      // Jede ROM-ID hat exakt 64 Bit
      while (currentBit <= 64)
      {
         int byteIndex {(currentBit - 1) / 8};
         int bitMask {1 << ((currentBit - 1) % 8)};

         uint8_t searchDirection {0};

         // Bestimme die Suchrichtung für das aktuelle Bit
         if (currentBit < lastDiscrepancy)
         {
            if (romId[byteIndex] & bitMask)
               searchDirection = 1;
         }
         else if (currentBit == lastDiscrepancy)
         {
            searchDirection = 1;
         }

         uint8_t status {0};

         if (wireTriplet(searchDirection, status) != success)
            return fail;

         // statusTSB und statusDIR extrahieren aus dem Triplet-Ergebnis
         bool bitRead {(status & statusSBR) != 0};
         bool complimentBitRead {(status & statusTSB) != 0};
         bool directionTaken {(status & statusDIR) != 0};

         // Fehlerzustand: Beide Bits antworten mit 1 -> Busfehler oder kein Gerät vorhanden
         if (bitRead && complimentBitRead)
            return fail;

         if (!bitRead && !complimentBitRead)
         {
            // Ein Konflikt liegt vor (0 und 1 sind auf dem Bus vorhanden)
            if (!directionTaken)
               discrepancyMarker = currentBit;
         }

         // Das eingeschlagene Bit in der aktuellen ROM-ID sichern
         if (directionTaken)
            romId[byteIndex] |= bitMask;
         else
            romId[byteIndex] &= ~bitMask;

         currentBit++;
      }

      // Wenn kein neuer Konfliktweg gefunden wurde, sind wir fertig
      if (discrepancyMarker == 0)
         done = true;

      lastDiscrepancy = discrepancyMarker;

      // Die gefundene 64-Bit ROM-ID in einen lesbaren Hex-String konvertieren (16 Zeichen)
      char romStr[17] {};
      snprintf(romStr, sizeof(romStr), "%02X%02X%02X%02X%02X%02X%02X%02X",
               romId[7], romId[6], romId[5], romId[4], romId[3], romId[2], romId[1], romId[0]);

      // Gefundenen Sensor in die Map eintragen und initialisieren
      SensorData data;
      data.active = true;
      data.value = 0.0;
      foundSensors[std::string(romStr)] = data;

      tell(eloDetail, "1-Wire Device discovered: %s", romStr);
   }

   return success;
}
