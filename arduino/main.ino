//***************************************************************************
// poold / Linux - Serial Interface
// File main.ino
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 17.09.2020 - Jörg Wendel
//***************************************************************************

#include <EEPROM.h>

#include "../aiservice.h"

//******************************************************************
// Interface
//******************************************************************

class cInterface : public cArduinoInterfaceService
{
   public:

      void processAiRequest(int input);
      void processCalRequest(int input);
      void processCalGetRequest(int input);
      void processCalSetRequest(int input);
      void dispatch();
      void setup();

   private:

      int intToA(int num);
      void calcGraphs();
      void blink(int t);
      int serialRead(void* buf, size_t size, int timeoutMs);

      float m[analogCount] {0};
      float b[analogCount] {0};
};

//******************************************************************
//
//******************************************************************

int cInterface::intToA(int num)
{
   switch (num)
   {
      case 0: return A0;
      case 1: return A1;
      case 2: return A2;
      case 3: return A3;
      case 4: return A4;
      case 5: return A5;
      case 6: return A6;
      case 7: return A7;
   }

   return A7; // ??
}

//******************************************************************
// Calculate Graphs
//******************************************************************

void cInterface::calcGraphs()
{
   CalSettings calSettings;

   for (unsigned int i = 0; i < analogCount; i++)
   {
      EEPROM.get(0 + (i*sizeof(CalSettings)), calSettings);

      m[i] = (calSettings.valueB - calSettings.valueA) / (calSettings.digitsB - calSettings.digitsA);
      b[i] = calSettings.valueB - m[i] * calSettings.digitsB;
   }
}

void cInterface::blink(int t)
{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(t);
  digitalWrite(LED_BUILTIN, LOW);
}

//******************************************************************
// Serial Read
//******************************************************************

int cInterface::serialRead(void* buf, size_t size, int timeoutMs)
{
   unsigned long timeoutAt = millis() + timeoutMs;
   char c;
   unsigned int nRead {0};

   while (nRead < size)
   {
      c = Serial.read();

      if (c  != -1)
      {
         ((char*)buf)[nRead] = c;
         nRead++;
      }
      else if (millis() > timeoutAt)
      {
        break;
      }
   }

   return nRead;
}

//******************************************************************
// AI Request
//******************************************************************

void cInterface::processAiRequest(int input)
{
   AnalogValue value;

   value.digits = analogRead(intToA(input));
   value.value = m[input] * value.digits + b[input];

   Header header(cAiResponse, input);
   header.id = comId;

   Serial.write((char*)&header, sizeof(Header));
   Serial.write((char*)&value, sizeof(AnalogValue));
}

//******************************************************************
// Callibartion
//******************************************************************

void cInterface::processCalRequest(int input)
{
   digitalWrite(LED_BUILTIN, HIGH);

   CalRequest calReq;
   CalResponse calResp;

   if (serialRead((char*)&calReq, sizeof(CalRequest), 1000) != sizeof(CalRequest))
      return ;

   unsigned int total {0};

   for (int i = 0; i < calReq.time; i++)
   {
      total += analogRead(intToA(input));
      delay(1000);
   }

   Header header(cCalResponse, input);
   header.id = comId;
   calResp.digits = total / calReq.time;       // the time is also the count of values (1 value / second)

   Serial.write((char*)&header, sizeof(Header));
   Serial.write((char*)&calResp, sizeof(CalResponse));

   digitalWrite(LED_BUILTIN, LOW);
}

void cInterface::processCalGetRequest(int input)
{
   CalSettings calSettings;

   EEPROM.get(0 + (input*sizeof(CalSettings)), calSettings);

   Header header(cCalGSResponse, input);
   header.id = comId;

   Serial.write((char*)&header, sizeof(Header));
   Serial.write((char*)&calSettings, sizeof(CalSettings));
}

void cInterface::processCalSetRequest(int input)
{
   CalSettings calSettings;

   if (serialRead((char*)&calSettings, sizeof(CalSettings), 1000) != sizeof(CalSettings))
      return ;

   EEPROM.put(0 + (input*sizeof(CalSettings)), calSettings);

   // recalculate graph by new callibartion settings

   calcGraphs();

   // respond with the new settings

   processCalGetRequest(input);
}

void cInterface::setup()
{
   Serial.begin(57600);

   while (!Serial)
      ;

   calcGraphs();

   Serial.flush();
   pinMode(LED_BUILTIN, OUTPUT);
}

void cInterface::dispatch()
{
   Header header(0, 0);

   // dispatcher

   if (Serial.available())
   {
      if (serialRead((char*)&header, sizeof(Header), 1000) != sizeof(Header))
         return ;

      if (header.id != comId)
         return ;

      blink(100);      // debug

      switch (header.command)
      {
         case cCalRequest:     processCalRequest(header.input);    break;
         case cCalGetRequest:  processCalGetRequest(header.input); break;
         case cCalSetRequest:  processCalSetRequest(header.input); break;
         case cAiRequest:      processAiRequest(header.input);     break;
         default: break;
      }
   }

   delay(100);
}

cInterface interface;

//******************************************************************
// setup
//******************************************************************

void setup()
{
   interface.setup();
}

//******************************************************************
// main loop
//******************************************************************

void loop()
{
   interface.dispatch();
}
