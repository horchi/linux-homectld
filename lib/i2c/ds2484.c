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
   // Read-Pointer explizit auf das Statusregister setzen. Ohne das würde nach einem
   // wireReadByte() (das den Pointer auf das Data-Register 0xE1 stehen lässt) hier
   // fälschlich das zuletzt gelesene Datenbyte statt des Status geliefert.
   uint8_t setPointer[2] {cmdSetReadPointer, regStatus};

   if (::write(fd, setPointer, 2) != 2)
   {
      tell(eloDebug, "Error: Setting read pointer to status register failed");
      return fail;
   }

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
      // WICHTIG: Winzige Pause für die Hardware, damit das Statusregister synchronisiert wird
      usleep(20);

      if (readStatus(status) != success)
         return fail;

      if (!(status & status1WB))
         return success;

      usleep(100);

      if (time(0) > timeoutAt)
      {
         tell(eloAlways, "Error: Timeout waiting for 1-Wire Bus to become free, resetting device");
         deviceReset();
         return fail;
      }
   } while (true);
}

//***************************************************************************
// 1-Wire Bus Reset
//***************************************************************************

int Ds2484::wireReset(bool& presenceDetected)
{
   uint8_t cmd = cmd1WireReset;

   presenceDetected = false;

   if (::write(fd, &cmd, 1) != 1)
   {
      tell(eloAlways, "Error: Sending 1-Wire Reset command failed");
      return fail;
   }

   uint8_t status {0};
   if (waitOnBusy(status) != success)
      return fail;

   if (status & statusSD)
   {
      tell(eloAlways, "Error: 1-Wire Short Circuit detected!");
      return fail;
   }

   presenceDetected = (status & statusPPD);

   // Dem DS2484 nach dem Einmessen des Presence-Pulses eine kurze Erholungspause
   // gönnen, BEVOR die CPU nach der Rückkehr sofort den nächsten Befehl schickt!
   usleep(200);

   return success;
}

//***************************************************************************
// 1-Wire Write Byte
//***************************************************************************

int Ds2484::wireWriteByte(uint8_t byte)
{
   // Korrektur: buf explizit als Array mit der Größe 2 deklarieren
   uint8_t buf[2] {};
   buf[0] = cmd1WireWriteByte;
   buf[1] = byte;

   uint8_t status {0};
   if (waitOnBusy(status) != success)
      return fail;

   if (::write(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: 1-Wire Write Byte failed");
      return fail;
   }

   // Dem Linux I2C-Treiber und der Hardware Zeit geben,
   // den Befehl physikalisch zu verarbeiten
   usleep(15);

   return waitOnBusy(status);
}

//***************************************************************************
// 1-Wire Read Byte
//***************************************************************************

int Ds2484::wireReadByte(uint8_t& byte)
{
   uint8_t cmd = cmd1WireReadByte; // 0x96
   uint8_t status {0};

   if (waitOnBusy(status) != success)
      return fail;

   // 1. Befehl an den DS2484 senden, um ein Byte vom 1-Wire Bus zu lesen
   if (::write(fd, &cmd, 1) != 1)
   {
      tell(eloAlways, "Error: Sending 1-Wire Read Byte command failed");
      return fail;
   }

   // Warten, bis der DS2484 das Byte komplett vom 1-Wire-Bus abgeholt hat
   if (waitOnBusy(status) != success)
      return fail;

   // 2. Den internen I2C-Pointer des DS2484 explizit auf das Datenregister (0xE1) setzen.
   // Ohne diesen Schritt liefert ein nachfolgender Lesezugriff oft nur den alten Status.
   uint8_t setPointerCmd[2] {};
   setPointerCmd[0] = cmdSetReadPointer; // Set Read Pointer Kommando
   setPointerCmd[1] = regReadData;       // Read Data Register Adresse

   if (::write(fd, setPointerCmd, 2) != 2)
   {
      tell(eloAlways, "Error: Setting I2C read pointer failed");
      return fail;
   }

   // 3. Das bereitgestellte Datenbyte aus dem Register auslesen
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

int Ds2484::wireTriplet(uint8_t searchDirection, uint8_t& status)
{
   // Korrektur: 2-Byte Array deklarieren für Befehl und Parameter
   uint8_t buf[2] {};
   buf[0] = cmd1WireTriplet; // Starr 0x78 laut Datenblatt

   if (searchDirection)
      buf[1] = 0x80; // Richtung 1: Bit 7 im Parameter-Byte setzen
   else
      buf[1] = 0x00; // Richtung 0: Parameter-Byte bleibt 0x00

   if (waitOnBusy(status) != success)
      return fail;

   // Sende Befehl und Parameter in einem einzigen I2C-Transfer (2 Bytes)
   if (::write(fd, buf, 2) != 2)
   {
      tell(eloAlways, "Error: 1-Wire Triplet command failed");
      return fail;
   }

   // Die physikalische Ausführungszeit für den 3-Bit-Hardwaretest abwarten
   usleep(120);

   // Den von der Hardware fertig berechneten Status abholen
   return readStatus(status);
}

uint8_t calculateCRC8(const uint8_t* data, uint8_t len)
{
   uint8_t crc {0};

   for (uint8_t i = 0; i < len; ++i)
   {
      uint8_t inbyte = data[i];

      for (uint8_t j = 0; j < 8; ++j)
      {
         uint8_t mix = (crc ^ inbyte) & 0x01;
         crc >>= 1;

         if (mix)
            crc ^= 0x8C; // Maxim-Standard-Polynom für 1-Wire

         inbyte >>= 1;
      }
   }

   return crc;
}

//***************************************************************************
// Search ROM (Finds all connected 1-Wire devices on the bus)
//***************************************************************************

int Ds2484::searchRom(SensorList& foundSensors)
{
   uint8_t romId[8];
   memset(romId, 0, 8);

   int lastDiscrepancy {0};
   bool done {false};

   foundSensors.clear();

   if (deviceReset() != success)
      return fail;

   while (!done)
   {
      bool presence {false};

      if (wireReset(presence) != success)
         return fail;

      if (!presence)
         return success;

      if (wireWriteByte(0xF0) != success)
         return fail;

      int currentBit {1};
      int discrepancyMarker {0};

      while (currentBit <= 64)
      {
         int byteIndex {(currentBit - 1) / 8};
         int bitMask {1 << ((currentBit - 1) % 8)};

         uint8_t searchDirection {0};

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

         bool bitRead {(status & statusSBR) != 0};
         bool complimentBitRead {(status & statusTSB) != 0};
         bool directionTaken {(status & statusDIR) != 0};

         if (bitRead && complimentBitRead)
            return fail;

         if (!bitRead && !complimentBitRead)
         {
            if (!directionTaken)
               discrepancyMarker = currentBit;
         }

         if (directionTaken)
            romId[byteIndex] |= bitMask;
         else
            romId[byteIndex] &= ~bitMask;

         currentBit++;
      }

      if (discrepancyMarker <= 0)
         done = true;

      lastDiscrepancy = discrepancyMarker;

      if (calculateCRC8(romId, 7) != romId[7])
      {
         tell(eloAlways, "Warning: 1-Wire CRC Check failed! Transmission error, skipping corrupt ID.");
         deviceReset();
         return fail;
      }

      // KORREKTUR: Rückwärts-Mapping der Seriennummer-Bytes (Index 6 bis 1),
      // um exakt Ihr Wunschformat "28-3c44f6494877" aus dem Speicher zu generieren!
      char romStr[16] {};
      snprintf(romStr, sizeof(romStr), "%02x-%02x%02x%02x%02x%02x%02x",
               romId[0], romId[6], romId[5], romId[4], romId[3], romId[2], romId[1]);

      SensorData data;
      data.active = true;
      data.value = 0.0;

      // Sichert die originale Byte-Reihenfolge für den Match-ROM Befehl
      memcpy(data.rawRom, romId, 8);
      foundSensors[std::string(romStr)] = data;

      tell(eloDetail, "1-Wire Device discovered: %s", romStr);

      if (discrepancyMarker == 0)
         return success;
   }

   return success;
}
