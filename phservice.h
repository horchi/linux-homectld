/*
 * phservice.h
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#pragma once

// #include "lib/common.h"

//***************************************************************************
// PH Board Service
//***************************************************************************

class cPhBoardService
{
   public:

      enum Misc
      {
         comId       = 0xef1a,
         analogCount = 8
      };

      enum Command
      {
         cCalRequest = 0x12,   // request the callibartion cycle
         cCalResponse,
         cCalGetRequest,       // request the current callibartion settings
         cCalGSResponse,       // Cal Get/Set Response
         cCalSetRequest,       // request set of callibartion settings
         cAiRequest,
         cAiResponse,

         cCount
      };

#pragma pack(1)

      struct Header
      {
         Header(char cmd, char in) { command = cmd; input = in; }
         word id {comId};
         char command {0};
         char input {0};
      };

      struct AnalogValue
      {
         float value {0.0};
         word digits {0};
      };

      struct CalRequest
      {
         int time {30};       // time for building average for calibration
      };

      struct CalResponse
      {
         word digits {0};     // average digits
      };

      struct CalSettings
      {
         word digitsA {377};    // value of calibration point A
         word digitsB {435};    // value of calibration point B
         float valueA {7.0};    //  digits for calibration point A
         float valueB {9.0};    //  digits for calibration point B
      };

#pragma pack()

};
