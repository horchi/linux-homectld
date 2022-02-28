//***************************************************************************
// Automation Control
// File specific.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 16.04.2020 - Jörg Wendel
//***************************************************************************

#pragma once

#include "daemon.h"

//***************************************************************************
// Class HomeCtl
//***************************************************************************

class HomeCtl : public Daemon
{
   public:

      HomeCtl();
      virtual ~HomeCtl();

      const char* myTitle() override { return "HomeCtl"; }
      int init() override;

      enum PinMappings
      {
         pinFilterPump   = pinGpio17,
         pinSolarPump    = pinGpio18,
         pinPoolLight    = pinGpio27,
         pinUVC          = pinGpio22,
         pinUserOut1     = pinGpio23,
         pinUserOut2     = pinGpio24,
         pinUserOut3     = pinGpio11,
         pinUserOut4     = pinGpio09,
         pinShower       = pinGpio25,
         pinLevel1       = pinGpio06,
         pinLevel2       = pinGpio12,
         pinLevel3       = pinGpio13,
         pinShowerSwitch = pinGpio19,
      };

#ifdef _POOL
      enum AnalogInputs
      {
         aiPh = 0,           // addr 0x00
         aiFilterPressure,   // addr 0x01
         aiU0,               // addr 0x02
         aiU1,               // addr 0x03
         aiU2,               // addr 0x04
         aiUser1,            // addr 0x05
         aiUser2,            // addr 0x06

         aiUser3,            // addr 0x07
         aiUser4,            // addr 0x08
         aiUser5,            // addr 0x09
         aiUser6             // addr 0x10
      };

      enum SpecialValues  // 'SP'
      {
         spWaterLevel = 1,
         spSolarDelta,
         spPhMinusDemand,
         spLastUpdate,
         spSolarPower,
         spSolarWork
      };
#endif

#ifdef _WOMO
   enum AnalogInputs
      {
         aiSolarCurrent = 0, // addr 0x00
         aiBattCurrent,      // addr 0x01
         aiBordnetz,         // addr 0x02
         aiFahrzeug,         // addr 0x03
         aiFreshWater,       // addr 0x04
         aiUser1,            // addr 0x05
         aiUser2,            // addr 0x06

         aiUser3,            // addr 0x07
         aiUser4,            // addr 0x08
         aiUser5,            // addr 0x09
         aiUser6,            // addr 0x10
      };

      enum SpecialValues  // 'SP'
      {
         spLastUpdate,   // 0x00
         spSolarAh,      // 0x01 0x01 solar Ampere Stunden (heute) [Ah]
         spSolarPower,   // 0x02 momentane Solar Leistung [W]
         spPower,        // 0x03 momentane Gesamt-Leistung [W]
         spBattBalanceAh // 0x04 actuelle Bilanz Batterie
      };
#endif

   protected:

      int initDb() override;
      int exitDb() override;

      int readConfiguration(bool initial) override;
      int applyConfigurationSpecials() override;
      int loadStates() override;
      int atMeanwhile() override;

      int process() override;
      int performJobs() override;
      void logReport() override;

      std::list<ConfigItemDef>* getConfiguration() override { return &configuration; }

#ifdef _POOL
      int calcWaterLevel();
      void phMeasurementActive();
      int calcPhMinusVolume(double ph);
      cDbStatement* selectSolarWorkPerDay {nullptr};

      int poolLightColorToggle {no};
      char* w1AddrPool {nullptr};
      char* w1AddrSolar {nullptr};
      char* w1AddrSuctionTube {nullptr};
      char* w1AddrAir {nullptr};

      double tPoolMax {28.0};
      double tSolarDelta {5.0};
      double massPerSecond {0.0};           // Fördermenge der Solarpumpe [kg·s-1] bzw. [l/s]

      int showerDuration {20};              // seconds
      int minSolarPumpDuration {10};        // minutes
      int deactivatePumpsAtLowWater {no};
      int alertSwitchOffPressure {0};

      // config - PH stuff

      double phMinusDensity {0.0};
      int phMinusDemand01 {0};         // Menge zum Senken um 0,1 [g]
      int phMinusDayLimit {0};
      int phPumpDuration100 {0};
      double phReference {0.0};        // PG Referenzwert (sollwert)

      int minPumpTimeForPh { 10 * tmeSecondsPerMinute }; // [s] #TODO -> add to config?

      std::vector<Range> filterPumpTimes;
      std::vector<Range> uvcLightTimes;
      std::vector<Range> poolLightTimes;

#endif

#ifdef _WOMO
      int batteryCapacity {220};
      cDbStatement* selectSolarAhPerDay {nullptr};
#endif

      static std::list<ConfigItemDef> configuration;
};
