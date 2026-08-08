//***************************************************************************
// Automation Control
// File service.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2010-2026 Jörg Wendel
//***************************************************************************

//***************************************************************************
// Class Service
//***************************************************************************

class Service
{
   public:

      enum SensorOptions
      {
         soNone   = 0x00,
         soSwitch = 0x01,
         soDim    = 0x02,
         soColor  = 0x04
      };

      enum WidgetType
      {
         wtUnknown = -1,
         wtSymbol  = 0,   // == 0
         wtChart,         // == 1
         wtText,          // == 2
         wtValue,         // == 3
         wtGauge,         // == 4
         wtMeter,         // == 5
         wtMeterLevel,    // == 6
         wtPlainText,     // == 7  without title
         wtChoice,        // == 8  option choice
         wtSymbolValue,   // == 9
         wtSpace,         // == 10
         wtTime,          // == 11  // dummy to display current time at WEBIF
         wtSymbolText,    // == 12
         wtChartBar,      // == 13
         wtSpecialSymbol, // == 14
         wtCount
      };
};
