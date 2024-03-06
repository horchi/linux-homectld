/* frameHandler.h
 *
 * Arduino library to read from Victron devices using VE.Direct protocol.
 * Derived from Victron framehandler reference implementation.
 *
 * 2020.05.05 - 0.2 - initial release
 * 2021.02.23 - 0.3 - change frameLen to 22 per VE.Direct Protocol version 3.30
 *
 */

#pragma once

#include "../common.h"

const byte frameLen {22};                        // VE.Direct Protocol: max frame size is 22
const byte nameLen {9};                          // VE.Direct Protocol: max name size is 9 including /0
const byte valueLen {33};                        // VE.Direct Protocol: max value size is 33 including /0
const byte buffLen {40};                         // Maximum number of lines possible from the device. Current protocol shows this to be the BMV700 at 33 lines.

class VeDirectFrameHandler
{
   public:

      VeDirectFrameHandler();
      int rxData(uint8_t inbyte);                // byte of serial data to be passed by the application

      char veName[buffLen][nameLen] {};          // public buffer for received names
      char veValue[buffLen][valueLen] {};        // public buffer for received values

      int frameIndex {0};                        // which line of the frame are we on
      int veEnd {0};                             // current size (end) of the public buffer

   private:

      enum States                                // state machine
      {
         IDLE,
         RECORD_BEGIN,
         RECORD_NAME,
         RECORD_VALUE,
         CHECKSUM,
         RECORD_HEX
      };

      int mState {IDLE};                         // current state
      uint8_t mChecksum {0};                     // checksum value
      char* mTextPointer {};                     // pointer to the private buffer we're writing to, name or value

      char mName[9] {};                          // buffer for the field name
      char mValue[33] {};                        // buffer for the field value
      char tempName[frameLen][nameLen];          // private buffer for received names
      char tempValue[frameLen][valueLen];        // private buffer for received values

      void textRxEvent(const char* mName, const char* mValue);
      void frameEndEvent(bool valid);
      void logE(const char* module, const char* error);
      bool hexRxEvent(uint8_t inbyte);

      size_t processedFrames {0};
};
