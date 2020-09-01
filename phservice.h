/*
 * phservice.h
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#pragma once

#include "lib/common.h"

//***************************************************************************
// PH Board Service
//***************************************************************************

class cPhBoardService
{
   public:

      enum Misc
      {
         comId = 0xef1a
      };

      enum Command
      {
         cPhRequest   = 0x10,  // request the current PH value
         cPhResponse,
         cPhCalRequest,        // request the callibartion cycle
         cPhCalResponse,
         cPhCalGetRequest,     // request the current callibartion settings
         cPhCalGetResponse,
         cPhCalSetRequest,     // request set of callibartion settings
         cPhCalSetResponse = cPhCalGetResponse,

         cCount
      };

#pragma pack(1)

      struct Header
      {
         word id {comId};
         char command {0};
      };

      struct PhValue
      {
         float ph {0.0};
         word value {0};
      };

      struct PhCalRequest
      {
         int time {30};      // time for building average for calibration
      };

      struct PhCalResponse
      {
         word value {0};
      };

      struct PhCalSettings
      {
         word pointA {377};   // value of calibration point A
         word pointB {435};   // value of calibration point B
         float phA {7.0};     // PH value for calibration point A
         float phB {9.0};     // PH value for calibration point B
      };

#pragma pack()

};
