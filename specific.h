//***************************************************************************
// Automation Control
// File specific.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2020-2024 - Jörg Wendel
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
      // int init() override;

#ifdef _POOL

      enum AnalogInputs
      {
         aiPh = 0x01
      };

      enum SpecialValues  // 'SP'
      {
         spPhMinusDemand = 3
         // spSolarPower = 4,
         // spSolarWork = 5
      };
#endif

   protected:

      // int initDb() override;
      // int exitDb() override;

      int readConfiguration(bool initial) override;
      int applyConfigurationSpecials() override;
      // int loadIoStates() override;
      // int atMeanwhile() override;

      int process(bool force = false, bool signal = false) override;
      // int performJobs() override;
      // void logReport() override;

      std::list<ConfigItemDef>* getConfiguration() override { return &configuration; }

#ifdef _POOL

    protected:

      void phMeasurementActive();
      int calcPhMinusVolume(double ph);
      // cDbStatement* selectSolarWorkPerDay {};

      // double alertSwitchOffPressure {0.0};
      // double massPerSecond {0.0};           // Fördermenge der Solarpumpe [kg·s-1] bzw. [l/s]

      double phMinusDensity {0.0};
      int phMinusDemand01 {0};                 // Menge zum Senken um 0,1 [g]
      int phMinusDayLimit {0};
      int phPumpDuration100 {0};
      double phReference {0.0};                // PG Referenzwert (sollwert)
      int minPumpTimeForPh {10 * tmeSecondsPerMinute}; // [s] #TODO -> add to config?

#endif

// #ifdef _WOMO
//       cDbStatement* selectSolarAhPerDay {};
// #endif

      static std::list<ConfigItemDef> configuration;
};
