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

      // GPIO names of raspberry pi
      //  on odroid pins are more or less identical wit other GPIO names

//       enum Pins       // we use the 'physical' PIN numbers here!
//       {
//          //               1         3.3 V
//          //               2         5 V
//          pinGpio02     =  3,     // GPIO2 (I²C 1 SDA)
//          //               4         5 V
//          pinGpio03     =  5,     // GPIO3 (I²C 1 SCL)
//          //               6         GND
//          pinGpio04     =  7,     // GPIO4 (W1)
//          pinGpio14     =  8,     // GPIO14 (TX)
//          //               9         GND
//          pinGpio15     = 10,     // GPIO15 (RX)
//          pinGpio17     = 11,     // GPIO17
//          pinGpio18     = 12,     // GPIO18
//          pinGpio27     = 13,     // GPIO27
//          //              14         GND
//          pinGpio22     = 15,     // GPIO22
//          pinGpio23     = 16,     // GPIO23
//          //              17         3.3 V
//          pinGpio24     = 18,     // GPIO24
//          pinGpio10     = 19,     // GPIO10
//          //              20         GND
//          pinGpio09     = 21,     // GPIO9
//          pinGpio25     = 22,     // GPIO25
//          pinGpio11     = 23,     // GPIO11
//          pinGpio08     = 24,     // GPIO8 (SPI)
//          //              25         GND
//          pinGpio07     = 26,     // GPIO7 (ID EEPROM)
//          pinIdSd       = 27,     // ID_SD  (I²C 2 SDA)
//          pinIdSc       = 28,     // ID_SC  (I²C 2 SCL)
//          pinGpio05     = 29,     // GPIO5
//          //              30         GND
//          pinGpio06     = 31,     // GPIO6
//          pinGpio12     = 32,     // GPIO12
//          pinGpio13     = 33,     // GPIO13
//          //              34         GND
//          pinGpio19     = 35,     // GPIO19
//          pinGpio16     = 36,     // GPIO16
//          pinGpio26     = 37,     // GPIO26  !!! at Odroid 'ADC.AIN.3'
//          pinGpio20     = 38,     // GPIO20  !!! at Odroid 'REF 1.8V'
//          //              39         GND
//          pinGpio21     = 40,     // GPIO21  !!! at Odroid 'ADC.AIN.2'

//          // aliases

//          pinW1           = pinGpio04,
//          pinW1Power      = pinGpio10,

//          pinMcpIrq       = pinGpio16,  // reserved for i2cmqtt !

//          // aliases for user in/out

//          pinUserOut1     = pinGpio23,
//          pinUserOut2     = pinGpio24,
//          pinUserOut3     = pinGpio11,
//          pinUserOut4     = pinGpio09,
//          pinUserOut5     = pinGpio05, // :(
//          pinUserOut6     = pinGpio06,

// #ifndef _POOL
//          pinUserOut7     = pinGpio07,
//          pinUserOut8     = pinGpio25,
//          pinUserOut9     = pinGpio19,
// #endif
//          pinUserOut10    = pinGpio20,
//          // diese beiden lassen wir erstmal weg
//          // sie können am Odroid nur 1,8 V können
//          // und dort als Analoge Eingänge verwendbar sind

//          // pinUserOut11    = pinGpio21,
//          // pinUserOut12    = pinGpio26,

//          pinUserInput1   = pinGpio12,  // :(
//          pinUserInput2   = pinGpio13,  // :(
//          pinUserInput3   = pinGpio08,

// #ifndef _POOL
//          pinUserInput4   = pinGpio17,  // :(
//          pinUserInput5   = pinGpio18,
//          pinUserInput6   = pinGpio27,
//          pinUserInput7   = pinGpio22,
// #endif
//       };

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
