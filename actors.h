//***************************************************************************
// Automation Control
// File actors.h
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2026 Jörg Wendel
//***************************************************************************

#pragma once

#include <sys/types.h>

#include "lib/common.h"

//***************************************************************************
// Actor Types
//   the light rules, scenes, the time control, the shading, the HASP panel and the WEBIF
//   ask the capabilities of a sensor type, not the type itself - a new bus system (e.g.
//   free@home) registers its types with a backend (HomeCtl::registerActorTypes())
//***************************************************************************

enum ActorCaps
{
   acNone       = 0x00,
   acLight      = 0x01,     // light: switch, brightness, dim (light rules, time control)
   acGroup      = 0x02,     // light group: toggled by the backend, state not reliable (on if any light is on)
   acBlind      = 0x04,     // blind: position 0..100 % (time control, shading)
   acRuleSource = 0x08,     // switch / motion sensor: events for the light rules
   acChecked    = 0x10      // commands of the rules / time control are checked by the reported state
};

//***************************************************************************
// Light Backend (e.g. deCONZ)
//   bri [%], transitiontime in 1/10 s
//***************************************************************************

class LightBackend
{
   public:

      virtual ~LightBackend() = default;

      virtual int toggle(const char* type, uint address, bool state, int bri = na, int transitiontime = na, bool explicitState = false) = 0;
      virtual int dim(const char* type, uint address, int inc, int transitiontime, bool switchOn = false) = 0;   // inc -254..254, 0 stops
      virtual int color(const char* type, uint address, int hue, int saturation, int bri) = 0;
      virtual int colorTemperature(const char* type, uint address, int ct, int bri) = 0;   // white mode, ct [mired]
};

//***************************************************************************
// Blind Backend (e.g. HomeMatic)
//   level [%] 0 = closed, 100 = open; noStop: the level also while the blind is moving
//   (else a command to a moving blind stops it)
//***************************************************************************

class BlindBackend
{
   public:

      virtual ~BlindBackend() = default;

      virtual int blindMove(const char* type, uint address, int level, bool noStop) = 0;
};

//***************************************************************************
// Actor Type
//***************************************************************************

struct ActorType
{
   uint caps {acNone};
   LightBackend* light {};
   BlindBackend* blind {};
};
