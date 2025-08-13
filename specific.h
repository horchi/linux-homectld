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
      int init() override;

      enum PinMappings
      {
         pinFilterPump   = pinGpio17,
         pinSolarPump    = pinGpio18,
         pinPoolLight    = pinGpio27,
         pinUVC          = pinGpio22,
         pinShower       = pinGpio25,
         pinShowerSwitch = pinGpio19
      };

#ifdef _POOL
      enum AnalogInputs
      {
         aiFilterPressure = 0x00,
         aiPh             = 0x01
      };

      enum SpecialValues  // 'SP'
      {
         spSolarDelta = 2,
         spPhMinusDemand,
         spSolarPower,
         spSolarWork
      };
#endif

   protected:

      int initDb() override;
      int exitDb() override;

      int readConfiguration(bool initial) override;
      int applyConfigurationSpecials() override;
      int loadIoStates() override;
      int atMeanwhile() override;

      int process(bool force = false) override;
      int performJobs() override;
      void logReport() override;

      std::list<ConfigItemDef>* getConfiguration() override { return &configuration; }

#ifdef _POOL
      void phMeasurementActive();
      int calcPhMinusVolume(double ph);
      cDbStatement* selectSolarWorkPerDay {};

      int poolLightColorToggle {no};
      std::string w1AddrPool;
      std::string w1AddrSolar;

      double tPoolMax {28.0};
      double tSolarDelta {5.0};
      double massPerSecond {0.0};           // Fördermenge der Solarpumpe [kg·s-1] bzw. [l/s]

      int showerDuration {20};              // seconds
      int minSolarPumpDuration {10};        // minutes
      int deactivatePumpsAtLowWater {no};
      double alertSwitchOffPressure {0.0};

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
      cDbStatement* selectSolarAhPerDay {};
#endif

      static std::list<ConfigItemDef> configuration;
};
