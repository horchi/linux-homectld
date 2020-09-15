
#include <EEPROM.h>

#include "../phservice.h"

//******************************************************************
// globals
//******************************************************************

float m {0};
float b {0};

//******************************************************************
// Calculate Graph
//******************************************************************

void calcGraph()
{
   cPhBoardService::PhCalSettings calSettings;

   EEPROM.get(0, calSettings);    // stored at eeprom address 0

   m = (calSettings.phB - calSettings.phA) / (calSettings.pointB - calSettings.pointA);
   b = calSettings.phB - m * calSettings.pointB;
}

//******************************************************************
// setup
//******************************************************************

void setup()
{
   Serial.begin(57600);

   while (!Serial)
      ;

   calcGraph();

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
// PH Pressure
//******************************************************************

void processPressureRequest()
{
   cPhBoardService::Header header;
   cPhBoardService::PressValue pressValue;

   pressValue.value = analogRead(A1);
//   phValue.ph = m * phValue.value + b;

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhResponse;

   Serial.write((char*)&header, sizeof(cPhBoardService::Header));
   Serial.write((char*)&pressValue, sizeof(cPhBoardService::PressValue));
}

//******************************************************************
// PH Request
//******************************************************************

void processPhRequest()
{
   cPhBoardService::Header header;
   cPhBoardService::PhValue phValue;

   phValue.value = analogRead(A0);
   phValue.ph = m * phValue.value + b;

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhResponse;

   Serial.write((char*)&header, sizeof(cPhBoardService::Header));
   Serial.write((char*)&phValue, sizeof(cPhBoardService::PhValue));
}

//******************************************************************
// Callibartion
//******************************************************************

void processCalRequest()
{
   digitalWrite(LED_BUILTIN, HIGH);

   cPhBoardService::Header header;
   cPhBoardService::PhCalRequest calReq;
   cPhBoardService::PhCalResponse calResp;

   if (serialRead((char*)&calReq, sizeof(cPhBoardService::PhCalRequest), 1000) != sizeof(cPhBoardService::PhCalRequest))
      return ;

   unsigned int total {0};

   for (int i = 0; i < calReq.time; i++)
   {
      total += analogRead(A0);
      delay(1000);
   }

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhCalResponse;
   calResp.value = total / calReq.time;   // the time is also the count of values (1 value / second)

   Serial.write((char*)&header, sizeof(cPhBoardService::Header));
   Serial.write((char*)&calResp, sizeof(cPhBoardService::PhCalResponse));

   digitalWrite(LED_BUILTIN, LOW);
}

void processCalGetRequest()
{
   cPhBoardService::Header header;
   cPhBoardService::PhCalSettings calSettings;

   EEPROM.get(0, calSettings);            // stored at eeprom address 0

   header.id = cPhBoardService::comId;
   header.command = cPhBoardService::cPhCalGetResponse;

   Serial.write((char*)&header, sizeof(cPhBoardService::Header));
   Serial.write((char*)&calSettings, sizeof(cPhBoardService::PhCalSettings));
}

void processCalSetRequest()
{
   cPhBoardService::PhCalSettings calSettings;

   if (serialRead((char*)&calSettings, sizeof(cPhBoardService::PhCalSettings), 1000) != sizeof(cPhBoardService::PhCalSettings))
      return ;

   EEPROM.put(0, calSettings);            // we store it at eeprom address 0

   // recalculate graph by new callibartion settings

   calcGraph();

   // respond with the new settings

   processCalGetRequest();
}

//******************************************************************
// main loop
//******************************************************************

void loop()
{
   cPhBoardService::Header header;

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
         case cPhBoardService::cPhRequest:       processPhRequest();     break;
         case cPhBoardService::cPhCalRequest:    processCalRequest();    break;
         case cPhBoardService::cPhCalGetRequest: processCalGetRequest(); break;
         case cPhBoardService::cPhCalSetRequest: processCalSetRequest(); break;
         default: break;
      }
   }

   delay(100);
}
