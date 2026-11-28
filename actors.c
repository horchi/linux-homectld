/*
 *  actors.c
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Actor types (actors.h)
 *
 *  The capabilities and the backend of the sensor types of the bus systems: the light
 *  rules, scenes, the time control, the shading, the HASP panel and the WEBIF ask them
 *  instead of the type ('DZL', 'HMB' ...). A type not registered is no actor.
 *
 *    DZL   deCONZ light          light, commands checked     (make switch DECONZ)
 *    DZLG  deCONZ group          light group                 (make switch DECONZ)
 *    DZS   deCONZ sensor         source of the light rules   (make switch DECONZ)
 *    FHL   free@home light       light                       (make switch FREEATHOME)
 *    FHB   free@home blind       blind                       (make switch FREEATHOME)
 *    FHS   free@home sensor      source of the light rules   (make switch FREEATHOME)
 *    HMB   HomeMatic blind       blind
 */

#include "daemon.h"

//***************************************************************************
// Register Actor Types
//***************************************************************************

void HomeCtl::registerActorTypes()
{
   actorTypes.clear();

   if (deconzAvailable)
   {
      actorTypes["DZL"]  = { acLight | acChecked, &deconz, nullptr };
      actorTypes["DZLG"] = { acLight | acGroup, &deconz, nullptr };
      actorTypes["DZS"]  = { acRuleSource, nullptr, nullptr };
   }

   if (freeAtHomeAvailable)            // no acChecked, the SysAP reports reliable
   {
      actorTypes["FHL"] = { acLight, &freeAtHome, nullptr };
      actorTypes["FHB"] = { acBlind, nullptr, &freeAtHome };
      actorTypes["FHS"] = { acRuleSource, nullptr, nullptr };
   }

   actorTypes["HMB"] = { acBlind, nullptr, &homeMaticBlinds };
}

//***************************************************************************
// Actor Type / Capabilities
//***************************************************************************

const ActorType* HomeCtl::actorType(const char* type) const
{
   if (isEmpty(type))
      return nullptr;

   auto it {actorTypes.find(type)};

   return it != actorTypes.end() ? &it->second : nullptr;
}

bool HomeCtl::hasCap(const char* type, uint caps) const
{
   const ActorType* at {actorType(type)};

   return at && (at->caps & caps);
}

//***************************************************************************
// HomeMatic Blinds
//***************************************************************************

int HomeCtl::HomeMaticBlinds::blindMove(const char* type, uint address, int level, bool noStop)
{
   return ctl->mqttNodeRedPublishAction(ctl->sensors[type][address], level, false, noStop);
}
