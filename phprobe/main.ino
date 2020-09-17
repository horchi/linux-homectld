
#include <EEPROM.h>

#include "../phservice.h"

//******************************************************************
// globals
//******************************************************************

float m[cPhBoardService::analogCount] {0};
float b[cPhBoardService::analogCount] {0};

//******************************************************************
//
//******************************************************************

int intToA(int num)
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

void calcGraphs()
{
   cPhBoardService::CalSettings calSettings;

   for (unsigned int i = 0; i < cPhBoardService::analogCount; i++)
   {
      EEPROM.get(0 + (i*sizeof(cPhBoardService::CalSettings)), calSettings);

      m[i] = (calSettings.valueB - calSettings.valueA) / (calSettings.digitsB - calSettings.digitsA);
      b[i] = calSettings.valueB - m[i] * calSettings.digitsB;
   }
}

//******************************************************************
// setup
//******************************************************************

void setup()
{
   Serial.begin(57600);

   while (!Serial)
      ;

   calcGraphs();

   Serial.flush();
   pinMode(LED_BUILTIN, OUTPUT);
}

void blink(int t)
{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(t);
  digitalWrite(LED_BUILTIN, LOW);
}

//******************************************************************
// Serial Read
//******************************************************************

int serialRead(void* buf, size_t size, int timeoutMs)
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

void processAiRequest(int input)
{
   cPhBoardService::AnalogValue value;

   value.digits = analogRead(intToA(input));
   value.value = m[input] * value.digits + b[input];

   cPhBoardService::Header header(cPhBoardService::cAiResponse, input);
   header.id = cPhBoardService::comId;

   Serial.write((char*)&header, sizeof(cPhBoardService::Header));
   Serial.write((char*)&value, sizeof(cPhBoardService::AnalogValue));
}

//******************************************************************
// Callibartion
//******************************************************************

void processCalRequest(int input)
{
   digitalWrite(LED_BUILTIN, HIGH);

   cPhBoardService::CalRequest calReq;
   cPhBoardService::CalResponse calResp;

   if (serialRead((char*)&calReq, sizeof(cPhBoardService::CalRequest), 1000) != sizeof(cPhBoardService::CalRequest))
      return ;

   unsigned int total {0};

   for (int i = 0; i < calReq.time; i++)
   {
      total += analogRead(intToA(input));
      delay(1000);
   }

   cPhBoardService::Header header(cPhBoardService::cCalResponse, input);
   header.id = cPhBoardService::comId;
   calResp.digits = total / calReq.time;       // the time is also the count of values (1 value / second)

   Serial.write((char*)&header, sizeof(cPhBoardService::Header));
   Serial.write((char*)&calResp, sizeof(cPhBoardService::CalResponse));

   digitalWrite(LED_BUILTIN, LOW);
}

void processCalGetRequest(int input)
{
   cPhBoardService::CalSettings calSettings;

   EEPROM.get(0 + (input*sizeof(cPhBoardService::CalSettings)), calSettings);

   cPhBoardService::Header header(cPhBoardService::cCalGSResponse, input);
   header.id = cPhBoardService::comId;

   Serial.write((char*)&header, sizeof(cPhBoardService::Header));
   Serial.write((char*)&calSettings, sizeof(cPhBoardService::CalSettings));
}

void processCalSetRequest(int input)
{
   cPhBoardService::CalSettings calSettings;

   if (serialRead((char*)&calSettings, sizeof(cPhBoardService::CalSettings), 1000) != sizeof(cPhBoardService::CalSettings))
      return ;

   EEPROM.put(0 + (input*sizeof(cPhBoardService::CalSettings)), calSettings);

   // recalculate graph by new callibartion settings

   calcGraphs();

   // respond with the new settings

   processCalGetRequest(input);
}

//******************************************************************
// main loop
//******************************************************************

void loop()
{
   cPhBoardService::Header header(0, 0);

   // dispatcher

   if (Serial.available())
   {
      if (serialRead((char*)&header, sizeof(cPhBoardService::Header), 1000) != sizeof(cPhBoardService::Header))
         return ;

      if (header.id != cPhBoardService::comId)
         return ;

      blink(100);      // debug

      switch (header.command)
      {
         case cPhBoardService::cCalRequest:     processCalRequest(header.input);     break;
         case cPhBoardService::cCalGetRequest:  processCalGetRequest(header.input);  break;
         case cPhBoardService::cCalSetRequest:  processCalSetRequest(header.input);  break;
         case cPhBoardService::cAiRequest:      processAiRequest(header.input);      break;
         default: break;
      }
   }

   delay(100);
}
