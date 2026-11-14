//***************************************************************************
// Automation Control
// File deconz.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 04.11.2010 - 15.12.2021  Jörg Wendel
//***************************************************************************

#include <algorithm>

#include "lib/curl.h"
#include "lib/json.h"

#include "websock.h"
#include "daemon.h"
#include "deconz.h"

cMyMutex Deconz::mapsMutex;

//***************************************************************************
// API documentation:
//    https://dresden-elektronik.github.io/deconz-rest-doc
//    https://dresden-elektronik.github.io/deconz-rest-doc/endpoints/lights/#example-request-data
//***************************************************************************

//***************************************************************************
// Problem, API-Key ist nach deCONZ Restart wieder weg
//  -> deCONZ trägt ihn nicht in die Tabelle ein
//  ?? Mache ich nich einen Fehler bei der Anmeldung ?
// Workaround manuell eintragen:
//   sqlite3 zll.db
//     insert into auth (apikey,createdate,lastusedate,devicetype,useragent) values('372245FAAA','2021-12-25T07:23:29','2021-12-25T07:23:29','homectrld','homectrld');
//   Dabei den Key (hier 372245FAAA) und ggf. das Datum anpassen!
//***************************************************************************

//***************************************************************************
// Deconz
//***************************************************************************

Deconz::Deconz()
{
}

Deconz::~Deconz()
{
   exitWsClient();
}

//***************************************************************************
// Init / Exit
//***************************************************************************

int Deconz::init(HomeCtl* parent, cDbConnection* connection)
{
   int status {success};

   daemon = parent;
   curl.init();

   tableDeconzLights = new cDbTable(connection, "deconzl");
   if (tableDeconzLights->open() != success) return fail;

   tableDeconzSensors = new cDbTable(connection, "deconzs");
   if (tableDeconzSensors->open() != success) return fail;

   // ------------------

   selectLightByUuid = new cDbStatement(tableDeconzLights);

   selectLightByUuid->build("select ");
   selectLightByUuid->bindAllOut();
   selectLightByUuid->bind("INSSP", cDBS::bndOut, ", ");     // meta field, not in bindAllOut() - knownSince()
   selectLightByUuid->build(" from %s where ", tableDeconzLights->TableName());
   selectLightByUuid->bind("UUID", cDBS::bndIn | cDBS::bndSet);

   status += selectLightByUuid->prepare();

   // ------------------

   selectSensorByUuid = new cDbStatement(tableDeconzSensors);

   selectSensorByUuid->build("select ");
   selectSensorByUuid->bindAllOut();
   selectSensorByUuid->bind("INSSP", cDBS::bndOut, ", ");     // meta field, not in bindAllOut() - knownSince()
   selectSensorByUuid->build(" from %s where ", tableDeconzSensors->TableName());
   selectSensorByUuid->bind("UUID", cDBS::bndIn | cDBS::bndSet);

   status += selectSensorByUuid->prepare();

   return status;
}

int Deconz::exit()
{
   curl.exit();

   delete tableDeconzLights;  tableDeconzLights = nullptr;
   delete tableDeconzSensors; tableDeconzSensors = nullptr;
   delete selectLightByUuid;  selectLightByUuid = nullptr;
   delete selectSensorByUuid; selectSensorByUuid = nullptr;

   return success;
}

//***************************************************************************
// Set Http Url
//   'host:port' of the REST API (config deconzHttpUrl), an 'http://' in front is skipped
//***************************************************************************

void Deconz::setHttpUrl(const char* url)
{
   httpUrl = url;

   if (httpUrl.find("http://") == 0)
      httpUrl = httpUrl.substr(strlen("http://"));

   while (!httpUrl.empty() && httpUrl.back() == '/')
      httpUrl.pop_back();
}

//***************************************************************************
// Query Api Key
//***************************************************************************

int Deconz::queryApiKey(std::string& result)
{
   json_t* jArray {};
   char* data {};
   asprintf(&data, "{ \"devicetype\": \"%s\" }", TARGET);

   if (post(jArray, "api", data) != success)
   {
      free(data);
      return fail;
   }

   free(data);
   json_t* jData = json_array_get(jArray, 0);
   const char* status {};
   json_t* jItem {};

   if (!jData)
   {
      json_decref(jArray);
      return fail;
   }

   int res {fail};

   json_object_foreach(jData, status, jItem)
   {
      if (strcmp(status, "success") != 0)
      {
         // e.g. {"error": {"type": 101, "address": "/", "description": "link button not pressed"}}

         result = getStringFromJson(jItem, "description", "");
         res = fail;
         break;
      }

      result = getStringFromJson(jItem, "username", "");
      res = result.empty() ? fail : success;
   }

   json_decref(jArray);

   return res;
}

//***************************************************************************
// Init Devices
//***************************************************************************

int Deconz::initDevices()
{
   json_t* jData {};

   if (query(jData, "sensors", apiKey.c_str()) != success)
      return fail;

   processDevices(jData, "sensor");
   json_decref(jData);

   if (query(jData, "lights", apiKey.c_str()) != success)
      return fail;

   processDevices(jData, "light");
   json_decref(jData);

   if (query(jData, "groups", apiKey.c_str()) != success)
      return fail;

   processDevices(jData, "light");
   json_decref(jData);

   // initWsClient();

   return done;
}

//***************************************************************************
// Process Devices
//***************************************************************************

int Deconz::processDevices(json_t* jData, std::string kind)
{
   const char* sid {};   // sensor id
   json_t* jItem {};

   json_object_foreach(jData, sid, jItem)
   {
      // "11": { "hascolor":false,"lastannounced":"2021-05-10T03:02:21Z","lastseen":"2021-12-16T17:18Z","manufacturername":"IKEA of Sweden","modelid":"TRADFRI bulb E14 W op/ch 400lm","name":"wandvorn","state":{"alert":"none","bri":125,"on":false,"reachable":true},"swversion":"1.2.214","type":"Dimmable light","uniqueid":"90:fd:9f:ff:fe:0f:76:9b-01"}
      // "12": { "config": { "battery": 100, "on": true, "reachable": true, "temperature": 2000 },
      //          "ep": 1, "lastseen": "2021-12-17T14:32Z", "manufacturername": "LUMI", "mode": 1, "modelid": "lumi.sensor_switch.aq2", "name": "swflur2",
      //          "state": { "buttonevent": 1002, "lastupdated": "2021-12-17T06:08:11.192" }, "type": "ZHASwitch", "uniqueid": "00:15:8d:00:01:e8:32:a1-01-0006" },
      // "20": {"colorcapabilities":0,"ctmax":65535,"ctmin":0,"etag":"7429bb9cf02aa4da5588bc2c2e517c39","hascolor":true,"lastannounced":null,"lastseen":"2022-01-10T16:45Z",
      //          "state":{"alert":"none","bri":254,"colormode":"ct","ct":250,"on":false,"reachable":true},
      //          "swversion":"2.0.022","type":"Color temperature light","uniqueid":"14:b4:57:ff:fe:7e:7a:57-01"}

      std::string dzType = getStringFromJson(jItem, "type", "");

      const char* uuid {};
      char* type {};

      // internal resources of deCONZ / Phoscon, no devices (the types are part of the REST API):
      //   'Daylight' (virtual sensor of the sun times), 'CLIPDaylightOffset' (day segments of
      //   Phoscon, 'Offset Ref P-SR' ...), 'Configuration tool' (the coordinator - ConBee / RaspBee)

      if (dzType == "Daylight" || dzType == "CLIPDaylightOffset" || dzType == "Configuration tool")
         continue;

      if (dzType == "LightGroup")
      {
         if (!isEmpty(getStringFromJson(jItem, "uniqueid")))
             continue;

         uuid = getStringFromJson(jItem, "id");
         asprintf(&type, "DZLG");
      }
      else
      {
         uuid = getStringFromJson(jItem, "uniqueid");
         asprintf(&type, "DZ%s", kind == "sensor" ? "S" : "L");
      }

      cDbTable* table = kind == "light" ? tableDeconzLights : tableDeconzSensors;
      cDbStatement* select = kind == "light" ? selectLightByUuid : selectSensorByUuid;

      table->clear();
      table->setValue("UUID", uuid);

      if (!select->find())
      {
         table->setValue("TYPE", type);
         table->insert();

         tell(eloDebugDeconz, "DECONZ: Added new '%s', uuid '%s'", kind.c_str(), uuid);

         table->clear();
         table->setValue("UUID", uuid);
         select->find();
      }

      uint address = table->getIntValue("ADDRESS");

      if (getObjectFromJson(jItem, "ctmin"))
      {
         json_t* jOpt = json_object();
         json_object_set_new(jOpt, "ctmin", json_integer(getIntFromJson(jItem, "ctmin")));
         json_object_set_new(jOpt, "ctmax", json_integer(getIntFromJson(jItem, "ctmax")));
         char* p = json_dumps(jOpt, JSON_REAL_PRECISION(4));
         table->setValue("OPTIONS", p);
         free(p);
         json_decref(jOpt);
      }

      table->setValue("KIND", kind.c_str());
      table->setValue("NAME", getStringFromJson(jItem, "name"));
      table->setValue("DZTYPE", dzType.c_str());
      table->setValue("MANUFACTURER", getStringFromJson(jItem, "manufacturername"));
      table->setValue("MODEL", getStringFromJson(jItem, "modelid"));
      table->store();

      {
         cMyMutexLock lock(&mapsMutex);

         if (kind == "light")
         {
            lights[uuid] = address;
            // tell(eloAlways, "DECONZ: Debug: Address of '%s' uuid '%s' is set to 0x%0x", type, uuid, address);
         }
         else
            sensors[uuid] = address;
      }

      tell(eloDebugDeconz, "DECONZ: Found '%s' %d with uuid '%s'", kind.c_str(), address, uuid);

      const char* unit{""};
      int so {HomeCtl::soNone};

      if (kind == "sensor")
      {
         if (dzType == "ZHAPresence")
            unit = "mov";
         else if (dzType == "ZHATemperature")
            unit = "°C";
         else if (dzType == "ZHAHumidity")
            unit = "%";
         else if (dzType == "ZHAPressure")
            unit = "hPa";
         else if (dzType == "ZHALightLevel")
            unit = "lx";
      }
      else if (kind == "light")
      {
         so = HomeCtl::soSwitch;

         // 'Color light', 'Extended color light' -> color (dimmable too), 'Color temperature light'
         //  has no hue / sat -> dimmable only

         if (strcasestr(dzType.c_str(), "color temperature"))
            so += HomeCtl::soDim;
         else if (strcasestr(dzType.c_str(), "color"))
            so += HomeCtl::soColor + HomeCtl::soDim; // assume all color lights are dimmable
         else if (strcasestr(dzType.c_str(), "dim"))
            so += HomeCtl::soDim;
      }

      daemon->addValueFact(address, type, 1, getStringFromJson(jItem, "name"), unit, ""/*title*/,
                           cWebService::urControl, nullptr, (HomeCtl::SensorOptions)so);

      int battery = getIntByPath(jItem, "config/battery", -1);

      // "state": { "alert": "none", "bri": 125, "on": false, "reachable": true }, }

      if (getObjectFromJson(jItem, "state"))
      {
         json_t* jsData = json_object();
         json_object_set_new(jsData, "type", json_string(type));
         json_object_set_new(jsData, "address", json_integer(address));

         if (battery != na)
            json_object_set_new(jsData, "battery", json_integer(battery));

         if (kind == "sensor")
         {
            // time of the last data at deCONZ (UTC, 'none' if never) - the init must not make old data fresh

            const char* lastUpdated {getStringByPath(jItem, "state/lastupdated")};
            struct tm tm {};

            if (!isEmpty(lastUpdated) && strptime(lastUpdated, "%Y-%m-%dT%H:%M:%S", &tm))
               json_object_set_new(jsData, "lastupdated", json_integer(timegm(&tm)));

            if (getObjectByPath(jItem, "state/temperature"))
               json_object_set_new(jsData, "value", json_real(getIntByPath(jItem, "state/temperature")/100.0));

            if (getObjectByPath(jItem, "state/pressure"))
               json_object_set_new(jsData, "value", json_real((double)getIntByPath(jItem, "state/pressure")));

            if (getObjectByPath(jItem, "state/humidity"))
               json_object_set_new(jsData, "value", json_real(getIntByPath(jItem, "state/humidity")/100.0));

            if (getObjectByPath(jItem, "state/lux"))
               json_object_set_new(jsData, "value", json_real((double)getIntByPath(jItem, "state/lux")));

            // if (getObjectByPath(jItem, "state/buttonevent"))
            //    json_object_set_new(jsData, "btnevent", json_integer(getIntByPath(jItem, "state/buttonevent")));

            // if (getObjectByPath(jItem, "state/presence"))
            //    json_object_set_new(jsData, "presence", json_boolean(getBoolByPath(jItem, "state/presence")));
         }
         else if (kind == "light")
         {
            json_object_set_new(jsData, "state", json_boolean(getBoolByPath(jItem, "state/on")));

            if (getObjectByPath(jItem, "state/bri"))
            {
               int bri = getIntByPath(jItem, "state/bri") / 255.0 * 100.0;
               json_object_set_new(jsData, "bri", json_integer(bri));
            }

            if (getObjectByPath(jItem, "state/hue"))
            {
               int hue = getIntByPath(jItem, "state/hue") / 65535.0 * 360.0;
               json_object_set_new(jsData, "hue", json_integer(hue));
            }

            if (getObjectByPath(jItem, "state/sat"))
            {
               int sat = getIntByPath(jItem, "state/sat") / 255.0 * 100.0;
               json_object_set_new(jsData, "sat", json_integer(sat));
            }

            // white mode (color temperature): the range of the light, some report a nonsense range
            //  (e.g. GLEDOPTO GL-S-007Z 0 - 65279) -> 153 - 454 mired (6500 - 2200 K)

            if (strcasestr(dzType.c_str(), "color") && !strcasestr(dzType.c_str(), "color temperature") &&
                (getObjectFromJson(jItem, "ctmin") || getObjectByPath(jItem, "state/ct")))
            {
               int ctMin {getIntFromJson(jItem, "ctmin", 0)};
               int ctMax {getIntFromJson(jItem, "ctmax", 0)};

               if (ctMin < 100 || ctMax > 1000 || ctMin >= ctMax)
               {
                  ctMin = 153;
                  ctMax = 454;
               }

               json_object_set_new(jsData, "ctmin", json_integer(ctMin));
               json_object_set_new(jsData, "ctmax", json_integer(ctMax));
            }

            if (getObjectByPath(jItem, "state/ct"))
               json_object_set_new(jsData, "ct", json_integer(getIntByPath(jItem, "state/ct")));

            if (getObjectByPath(jItem, "state/colormode"))
               json_object_set_new(jsData, "colormode", json_string(getStringByPath(jItem, "state/colormode")));
         }

         // don't trigger switches (the last button event would be a new event for the light rules),
         //  only their battery is taken

         if (dzType == "ZHASwitch")
         {
            json_object_del(jsData, "btnevent");
            json_object_del(jsData, "value");
         }

         if (dzType != "ZHASwitch" || battery != na)
         {
            char* p = json_dumps(jsData, JSON_REAL_PRECISION(4));

            {
               cMyMutexLock lock(&messagesInMutex);
               messagesIn.push(p);
            }

            free(p);
         }

         json_decref(jsData);

      }

      free(type);
   }

   return done;
}

//***************************************************************************
// Toggle
//***************************************************************************

//***************************************************************************
// MAC Of
//   the MAC of a light (DZL) or sensor (DZS) as the deCONZ log shows it, e.g. '00158D0002310239'
//   (the uniqueid without endpoint / cluster and colons)
//***************************************************************************

std::string Deconz::macOf(const char* type, uint address)
{
   cMyMutexLock lock(&mapsMutex);

   const std::map<std::string,uint>* map {strcmp(type, "DZL") == 0 ? &lights : strcmp(type, "DZS") == 0 ? &sensors : nullptr};

   if (!map)
      return "";

   for (const auto& [uuid, addr] : *map)
   {
      if (addr != address)
         continue;

      std::string mac;

      for (char c : uuid.substr(0, uuid.find('-')))
         if (c != ':')
            mac += toupper(c);

      return mac;
   }

   return "";
}

int Deconz::toggle(const char* type, uint address, bool state, int bri, int transitiontime, bool explicitState)
{
   json_t* jArray {};
   std::string uuid;

   {
      cMyMutexLock lock(&mapsMutex);

      for (const auto& light : lights)
      {
         if (light.second == address)
         {
            uuid = light.first;
            break;
         }
      }
   }

   if (bri != na && bri < 5)
      state = false;

   json_t* jObj = json_object();

   if (std::string(type) == "DZL" || explicitState)   // a group is toggled unless the state is explicit
      json_object_set_new(jObj, "on", json_boolean(state));
   else
      json_object_set_new(jObj, "toggle", json_boolean(true));

   if (transitiontime != na)
      json_object_set_new(jObj, "transitiontime", json_integer(transitiontime));

   if (bri != na && state)
   {
      int dim = (int)((255 / 100.0) * bri);
      json_object_set_new(jObj, "bri", json_integer(dim));
   }

   int status {success};

   if ((status = put(type, jArray, uuid.c_str(), jObj)) == success)
      status = checkResult(jArray);

   if (status == success)
      tell(eloDeconz, "DECONZ: Toggled '%s' to %s", uuid.c_str(), state ? "on" : "off");

   json_decref(jArray);

   return status;
}

//***************************************************************************
// Color
//***************************************************************************

//***************************************************************************
// Dim
//   'bri_inc' of deCONZ: changes the brightness by inc (-254 .. 254) within the transition time
//   [1/10 s], 0 stops a running change (dimming while a button is hold); switchOn: an off light
//   is switched on with the lowest brightness first
//***************************************************************************

int Deconz::dim(const char* type, uint address, int inc, int transitiontime, bool switchOn)
{
   std::string uuid;

   {
      cMyMutexLock lock(&mapsMutex);

      for (const auto& light : lights)
      {
         if (light.second == address)
         {
            uuid = light.first;
            break;
         }
      }
   }

   if (uuid.empty())
      return fail;

   json_t* jArray {};
   int status {success};

   if (switchOn)
   {
      if ((status = put(type, jArray, uuid.c_str(), json_pack("{s:b,s:i}", "on", 1, "bri", 1))) == success)
         status = checkResult(jArray);

      json_decref(jArray);
      jArray = nullptr;

      if (status != success)
         return status;
   }

   json_t* jObj {json_object()};
   json_object_set_new(jObj, "bri_inc", json_integer(std::clamp(inc, -254, 254)));

   if (inc != 0 && transitiontime != na)
      json_object_set_new(jObj, "transitiontime", json_integer(transitiontime));

   if ((status = put(type, jArray, uuid.c_str(), jObj)) == success)
      status = checkResult(jArray);

   json_decref(jArray);

   tell(eloDeconz, "DECONZ: Dim '%s' by %d (%d)", uuid.c_str(), inc, transitiontime);

   return status;
}

int Deconz::color(const char* type, uint address, int hue, int sat, int bri)
{
   json_t* jArray {};
   std::string uuid;

   {
      cMyMutexLock lock(&mapsMutex);

      for (const auto& light : lights)
      {
         if (light.second == address)
         {
            uuid = light.first;
            break;
         }
      }
   }

   sat = (int)((255 / 100.0) * sat);
   hue = (65535 / 360) * hue;

   json_t* jObj = json_object();
   json_object_set_new(jObj, "on", json_boolean(true));
   json_object_set_new(jObj, "hue", json_integer(hue));
   json_object_set_new(jObj, "sat", json_integer(sat));

   if (bri != na)                // na -> the light keeps its brightness
      json_object_set_new(jObj, "bri", json_integer((int)((255 / 100.0) * bri)));

   int status {success};

   if ((status = put(type, jArray, uuid.c_str(), jObj)) == success)
      status = checkResult(jArray);

   if (status == success)
      tell(eloDeconz, "DECONZ: Changed color of %s to %d", uuid.c_str(), hue);

   json_decref(jArray);

   return success;
}

//***************************************************************************
// Color Temperature
//   white mode of a color light (RGB+CCT, its own white LEDs), ct [mired], bri [%]
//***************************************************************************

int Deconz::colorTemperature(const char* type, uint address, int ct, int bri)
{
   json_t* jArray {};
   std::string uuid;

   {
      cMyMutexLock lock(&mapsMutex);

      for (const auto& light : lights)
      {
         if (light.second == address)
         {
            uuid = light.first;
            break;
         }
      }
   }

   if (uuid.empty())
      return fail;

   json_t* jObj = json_object();
   json_object_set_new(jObj, "on", json_boolean(true));
   json_object_set_new(jObj, "ct", json_integer(ct));

   if (bri > 0)
      json_object_set_new(jObj, "bri", json_integer((int)((255 / 100.0) * std::min(bri, 100))));

   int status {success};

   if ((status = put(type, jArray, uuid.c_str(), jObj)) == success)
      status = checkResult(jArray);

   if (status == success)
      tell(eloDeconz, "DECONZ: Changed color temperature of %s to %d mired", uuid.c_str(), ct);

   json_decref(jArray);

   return status;
}

//***************************************************************************
// Check Result
//***************************************************************************

int Deconz::checkResult(json_t* jArray)
{
   json_t* jData = json_array_get(jArray, 0);
   const char* status {};
   json_t* jItem {};

   if (!jData)
      return fail;

   json_object_foreach(jData, status, jItem)
   {
      if (strcmp(status, "success") != 0)
      {
         tell(eloAlways, "DECONZ: Error: '%s', address was '%s'", getStringFromJson(jItem, "description", "<unknown error>"), getStringFromJson(jItem, "address"));
         return fail;
      }
   }

   return success;
}

//***************************************************************************
// Put
//***************************************************************************

int Deconz::put(const std::string& type, json_t*& jResult, const char* uuid, json_t* jData)
{
   // /api/<apikey>/lights/<id>/state

   char* url {};

   if (type == "DZL")
      asprintf(&url, "http://%s/api/%s/lights/%s/state", httpUrl.c_str(), apiKey.c_str(), uuid);
   else
      asprintf(&url, "http://%s/api/%s/groups/%s/action", httpUrl.c_str(), apiKey.c_str(), uuid);

   tell(eloDebugDeconz, "DECONZ: REST call '%s'", url);

   char* payload = json_dumps(jData, JSON_REAL_PRECISION(4));
   json_decref(jData);
   std::string data;

   if (curl.put(url, payload, &data) != success)
   {
      tell(eloAlways, "DECONZ: REST call to '%s' failed", url);
      free(url);
      return fail;
   }

   tell(eloDeconz, "DECONZ: -> put '%s' '%s'; result [%s]", url, payload, data.c_str());
   free(url);
   free(payload);

   jResult = jsonLoad(data.c_str());

   if (!jResult)
      return fail;

   return success;
}

//***************************************************************************
// Post
//***************************************************************************

int Deconz::post(json_t*& jResult, const char* method, const char* payload)
{
   std::string data;

   // http://192.168.200.101:8081/api/<method>

   char* url {};
   asprintf(&url, "http://%s/%s", httpUrl.c_str(), method);
   tell(eloDebugDeconz, "DECONZ: REST call '%s'", url);

   if (curl.post(url, payload, &data) != success)
   {
      tell(eloAlways, "DECONZ: REST call to '%s' failed", url);
      free(url);
      return fail;
   }

   tell(eloDeconz, "-> (DECONZ) '%s' '%s' [%s]", url, payload, data.c_str());
   free(url);

   jResult = jsonLoad(data.c_str());

   if (!jResult)
      return fail;

   return done;
}

//***************************************************************************
// Query
//***************************************************************************

int Deconz::query(json_t*& jResult, const char* method, const char* key)
{
   MemoryStruct data;
   int size {0};

   // http://192.168.200.101:8081/api/<key>/<method>

   char* url {};
   asprintf(&url, "http://%s/api/%s/%s", httpUrl.c_str(), apiKey.c_str(), method);
   tell(eloDebugDeconz, "DECONZ: REST call '%s'", url);

   if (curl.downloadFile(url, size, &data, 5) != success)
   {
      tell(eloAlways, "DECONZ: REST call to '%s' failed", url);
      free(url);
      return fail;
   }

   tell(eloDeconz, "DECONZ: <- '%s' [%s]", url, data.memory);
   free(url);
   jResult = jsonLoad(data.memory);

   if (!jResult)
      return fail;

   return done;
}

//***************************************************************************
// REST
//   method GET | PUT | POST | DELETE on http://<host:port>/api/<key>/<path>, jData (taken, may
//   be null) is the body; the result is the JSON of deCONZ (an array of success / error objects
//   for PUT / POST / DELETE, the resource(s) for GET)
//***************************************************************************

int Deconz::rest(const char* method, const char* path, json_t* jData, json_t*& jResult)
{
   jResult = nullptr;

   if (httpUrl.empty() || apiKey.empty())
   {
      if (jData)
         json_decref(jData);

      return fail;
   }

   char* url {};
   asprintf(&url, "http://%s/api/%s/%s", httpUrl.c_str(), apiKey.c_str(), path);

   char* payload {jData ? json_dumps(jData, JSON_REAL_PRECISION(4)) : strdup("")};

   if (jData)
      json_decref(jData);

   std::string data;
   int status {fail};

   if (strcmp(method, "GET") == 0)
   {
      MemoryStruct mem;
      int size {0};

      if ((status = curl.downloadFile(url, size, &mem, 5)) == success && mem.memory)
         data.assign(mem.memory, mem.size);
   }
   else if (strcmp(method, "PUT") == 0)
      status = curl.put(url, payload, &data);
   else if (strcmp(method, "POST") == 0)
      status = curl.post(url, payload, &data);
   else if (strcmp(method, "DELETE") == 0)
      status = curl.del(url, payload, &data);

   tell(eloDeconz, "DECONZ: -> %s '%s' '%s'; result [%s]", method, url, payload, data.c_str());

   if (status != success)
      tell(eloAlways, "DECONZ: REST call %s '%s' failed", method, url);

   free(url);
   free(payload);

   if (status != success)
      return fail;

   jResult = jsonLoad(data.c_str());

   return jResult ? success : fail;
}

//***************************************************************************
// Result Error
//   [{"error": {"address": "/lights/7", "description": "resource, /lights/7, not available", "type": 3}}]
//***************************************************************************

std::string Deconz::resultError(json_t* jArray)
{
   if (!jArray)
      return "keine Antwort von deCONZ";

   if (!json_is_array(jArray))
      return "";

   size_t i {0};
   json_t* jItem {};

   json_array_foreach(jArray, i, jItem)
   {
      json_t* jError {json_object_get(jItem, "error")};

      if (jError)
         return getStringFromJson(jError, "description", "unbekannter Fehler");
   }

   return "";
}

//***************************************************************************
// Address Of / Forget
//***************************************************************************

long Deconz::addressOf(const char* kind, const char* uuid)
{
   cMyMutexLock lock(&mapsMutex);

   const std::map<std::string,uint>& map {strcmp(kind, "light") == 0 ? lights : sensors};
   auto it {map.find(uuid)};

   return it != map.end() ? (long)it->second : (long)na;
}

time_t Deconz::knownSince(const char* kind, const char* uuid)
{
   cDbTable* table {strcmp(kind, "light") == 0 ? tableDeconzLights : tableDeconzSensors};
   cDbStatement* select {strcmp(kind, "light") == 0 ? selectLightByUuid : selectSensorByUuid};

   table->clear();
   table->setValue("UUID", uuid);

   time_t since {select->find() ? (time_t)table->getIntValue("INSSP") : 0};

   select->freeResult();
   table->reset();

   return since;
}

void Deconz::forget(const char* kind, const char* uuid)
{
   {
      cMyMutexLock lock(&mapsMutex);

      if (strcmp(kind, "light") == 0)
         lights.erase(uuid);
      else
         sensors.erase(uuid);
   }

   cDbTable* table {strcmp(kind, "light") == 0 ? tableDeconzLights : tableDeconzSensors};
   cDbStatement* select {strcmp(kind, "light") == 0 ? selectLightByUuid : selectSensorByUuid};

   table->clear();
   table->setValue("UUID", uuid);

   if (select->find())
      table->deleteWhere("%s = %ld", table->getField("ADDRESS")->getDbName(), table->getIntValue("ADDRESS"));

   select->freeResult();
   table->reset();
}

lws* Deconz::client_wsi {};
std::string Deconz::wsHost;
int Deconz::wsPort {443};
std::atomic<bool> Deconz::connected {false};
lws_context* Deconz::context {};
Deconz* Deconz::singleton {};

cMyMutex Deconz::messagesInMutex;
std::queue<std::string> Deconz::messagesIn;
constexpr const char* protocollName {"deConz"};
static lws_sorted_usec_list_t sul;

//***************************************************************************
// Init / Exit Ws Client
//***************************************************************************

static lws_retry_bo_t retry {
   .secs_since_valid_ping = 3,
   .secs_since_valid_hangup = 10,
};

void callbackConnect(lws_sorted_usec_list_t* _sul)
{
   lws_client_connect_info ci;
   memset(&ci, 0, sizeof(ci));

   ci.context = Deconz::context;
   ci.port = Deconz::wsPort;
   ci.address = Deconz::wsHost.c_str();
   ci.path = "/";
   ci.host = ci.address;
   ci.origin = ci.address;
   ci.protocol = protocollName;
   ci.alpn = "h2;http/1.1";
   ci.pwsi = &Deconz::client_wsi;
   ci.retry_and_idle_policy = &retry;

   // tell(eloAlways, "calling lws_client_connect_via_info with context %p", (void*)ci.context);

   if (!lws_client_connect_via_info(&ci))
   {
      lws_sul_schedule(Deconz::context, 0, _sul, callbackConnect, 5 * LWS_USEC_PER_SEC);
      return;
   }

   tell(eloAlways, "DECONZ: WS connection established");
}

int Deconz::initWsClient()
{
   Deconz::singleton = this;

   // the websocket runs on the host of the REST API, its port is reported by deCONZ
   //   (config 'websocketport', set in deCONZ resp. the docker container), 443 if it can't be queried

   wsHost = httpUrl.substr(0, httpUrl.find_first_of(":/"));
   wsPort = 443;

   json_t* jConfig {};

   if (query(jConfig, "config", apiKey.c_str()) == success && jConfig)
   {
      int port = getIntFromJson(jConfig, "websocketport", na);

      if (port > 0 && port < 65536)
         wsPort = port;
      else
         tell(eloAlways, "DECONZ: Warning: No websocket port in the config of deCONZ, using %d", wsPort);

      json_decref(jConfig);
   }
   else
      tell(eloAlways, "DECONZ: Warning: Query of the config failed, using websocket port %d", wsPort);

   tell(eloAlways, "DECONZ: Websocket at '%s:%d'", wsHost.c_str(), wsPort);

   int logs {LLL_ERR | LLL_WARN | LLL_USER}; // {LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE};
   lws_set_log_level(logs, NULL);

   memset(&protocols, 0, sizeof(protocols));
   protocols[0].name = protocollName;
   protocols[0].callback = callbackDeconzWs;

   lws_context_creation_info info;
	memset(&info, 0, sizeof(info));
	info.options = 0;
	info.port = CONTEXT_PORT_NO_LISTEN;  // client side - no listener
	info.protocols = protocols;

	/*
	 * since we know this lws context is only ever going to be used with
	 * one client wsis / fds / sockets at a time, let lws know it doesn't
	 * have to use the default allocations for fd tables up to ulimit -n.
	 * It will just allocate for 1 internal and 1 (+ 1 http2 nwsi) that we
	 * will use.
	 */

	info.fd_limit_per_thread = 1 + 1 + 1;
	context = lws_create_context(&info);

	if (!context)
   {
		tell(eloAlways, "DECONZ: WS init failed");
		return fail;
	}

   tell(eloAlways, "DECONZ: context created");

   // connectClient();
	lws_sul_schedule(context, 0, &sul, callbackConnect, 100);

   threadCtl.deconz = this;
   threadCtl.timeout = 30;

   if (pthread_create(&syncThread, NULL, syncFct, &threadCtl))
   {
      tell(eloAlways, "DECONZ: Error: Failed to start client daemon thread");
      return fail;
   }

	return success;
}

int Deconz::exitWsClient()
{
   if (syncThread)
   {
      threadCtl.close = true;
      lws_cancel_service(context);

      time_t endWait = time(0) + 2;  // 2 second for regular end

      while (threadCtl.active && time(0) < endWait)
         usleep(100);

      if (threadCtl.active)
         pthread_cancel(syncThread);
      else
         pthread_join(syncThread, 0);

      syncThread = 0;
   }

	lws_context_destroy(context);
   context = nullptr;
   return done;
}

//***************************************************************************
// Sync
//***************************************************************************

void* Deconz::syncFct(void* user)
{
   ThreadControl* threadCtl = (ThreadControl*)user;
   threadCtl->active = true;

   tell(eloDebugDeconz, "DECONZ: Started syncThread");

   while (!threadCtl->close)
      lws_service(context, 0);

   threadCtl->active = false;
   tell(eloDebugDeconz, "DECONZ:  Stopped syncThread regular");

   return nullptr;
}

int Deconz::atInMessage(const char* data, size_t size)
{
   // {"e":"changed","id":"3","r":"lights","state":{"alert":null,"bri":80,"on":true,"reachable":true},"t":"event","uniqueid":"00:0b:57:ff:fe:d5:39:21-01"}
   // {"e":"changed","id":"26","r":"lights","state":{"alert":null,"bri":244,"colormode":"hs","ct":500,"effect":"none","hue":64581,"on":true,"reachable":true,"sat":18,"xy":[0.3274,0.3218]},"t":"event","uniqueid":"00:12:4b:00:1e:d0:03:dd-0b"}
   // {"e":"changed","id":"31","r":"sensors","state":{"buttonevent":1002,"lastupdated":"2021-12-15T18:30:58.779"},"t":"event","uniqueid":"00:15:8d:00:02:13:87:2c-01-0012"}
   // {"e":"changed","id":"33","r":"sensors","state":{"lastupdated":"2021-12-16T18:34:17.872","presence":true},"t":"event","uniqueid":"00:15:8d:00:02:57:b7:41-01-0406"}
   // {"e":"changed","id":"8","r":"groups","state":{"all_on":false,"any_on":false},"t":"event"}

   if (strcmp(data, "WS CONNECTED") == 0)
   {
      cMyMutexLock lock(&messagesInMutex);
      messagesIn.push(data);
      return done;
   }

   json_t* obj = jsonLoad(data, size);

   tell(eloDeconz, "DECONZ: Debug: Got [%s]", data);

   if (!obj)
      return fail;

   // process message, build message

   std::string resource = getStringFromJson(obj, "r");
   const char* uuid = getStringFromJson(obj, resource == "groups" ? "id" : "uniqueid");
   const char* t = getStringFromJson(obj, "t");
   const char* event = getStringFromJson(obj, "e");

   long address {na};

   // a new light / sensor ({"e":"added","r":"sensors","id":"41","sensor":{...},"t":"event","uniqueid":"..."})
   //  resp. a deleted one ({"e":"deleted","r":"lights","id":"7","t":"event","uniqueid":"..."}) - paired / removed
   //  by the setup page 'Zigbee' or by Phoscon; processed by the main thread (REST calls, database),
   //  see HomeCtl::zigbeeEvent()

   if ((resource == "lights" || resource == "sensors") && !isEmpty(t) && strcmp(t, "event") == 0 &&
       !isEmpty(event) && (strcmp(event, "added") == 0 || strcmp(event, "deleted") == 0))
   {
      json_t* jEvent {json_object()};
      json_object_set_new(jEvent, "dzevent", json_string(event));
      json_object_set_new(jEvent, "resource", json_string(resource.c_str()));
      json_object_set_new(jEvent, "id", json_string(getStringFromJson(obj, "id", "")));
      json_object_set_new(jEvent, "uniqueid", json_string(uuid ? uuid : ""));

      json_t* jItem {json_object_get(obj, resource == "lights" ? "light" : "sensor")};

      if (jItem)
         json_object_set(jEvent, "item", jItem);

      char* p {json_dumps(jEvent, JSON_REAL_PRECISION(4))};

      {
         cMyMutexLock lock(&messagesInMutex);
         messagesIn.push(p);
      }

      free(p);
      json_decref(jEvent);
      json_decref(obj);

      return success;
   }

   // we start with 'sensors' and 'lights', later on may be 'groups'

   {
      cMyMutexLock lock(&mapsMutex);

      // find() - an unknown uuid (e.g. the 'Daylight' sensor, a not yet registered device) must not be
      //  added to the maps with address 0

      std::map<std::string,uint>* map {resource == "sensors" ? &sensors : resource == "lights" || resource == "groups" ? &lights : nullptr};

      if (!map || isEmpty(uuid))
      {
         json_decref(obj);
         return done;
      }

      auto it {map->find(uuid)};

      if (it == map->end())
      {
         tell(eloDebugDeconz, "DECONZ: Ignoring event of unknown '%s' uuid '%s'", resource.c_str(), uuid);
         json_decref(obj);
         return done;
      }

      address = it->second;

      tell(eloDebugDeconz, "DECONZ: Got address %ld for uuid '%s'", address, uuid);
   }

   // state changes, for sensors also changes of the battery ({"config":{"battery":..}})

   bool hasBattery {resource == "sensors" && getObjectByPath(obj, "config/battery")};

   if (!getObjectFromJson(obj, "state") && !hasBattery)
   {
      json_decref(obj);
      return done;
   }

   if (address == na || strcmp(t, "event") != 0 || strcmp(event, "changed") != 0)
   {
      tell(eloAlways, "DECONZ: Debug: Ignoring '%s' !!", uuid);
      json_decref(obj);
      return done;
   }

   json_t* jData = json_object();
   char* type {};
   asprintf(&type, "DZ%s", resource == "sensors" ? "S" : resource == "groups" ? "LG" : "L");

   json_object_set_new(jData, "type", json_string(type));
   json_object_set_new(jData, "address", json_integer(address));

   if (resource == "sensors")
   {
      if (hasBattery)
         json_object_set_new(jData, "battery", json_integer(getIntByPath(obj, "config/battery")));

      if (getObjectByPath(obj, "state/buttonevent"))
         json_object_set_new(jData, "btnevent", json_integer(getIntByPath(obj, "state/buttonevent")));

      if (getObjectByPath(obj, "state/temperature"))
         json_object_set_new(jData, "value", json_real(getIntByPath(obj, "state/temperature")/100.0));

      if (getObjectByPath(obj, "state/pressure"))
         json_object_set_new(jData, "value", json_real((double)getIntByPath(obj, "state/pressure")));

      if (getObjectByPath(obj, "state/humidity"))
         json_object_set_new(jData, "value", json_real(getIntByPath(obj, "state/humidity")/100.0));

      if (getObjectByPath(obj, "state/lux"))
         json_object_set_new(jData, "value", json_real((double)getIntByPath(obj, "state/lux")));

      if (getObjectByPath(obj, "state/presence"))
         json_object_set_new(jData, "presence", json_boolean(getBoolByPath(obj, "state/presence")));
   }
   else if (resource == "groups")
   {
      json_object_set_new(jData, "state", json_boolean(getBoolByPath(obj, "state/any_on")));
   }
   else
   {
      json_object_set_new(jData, "state", json_boolean(getBoolByPath(obj, "state/on")));

      if (getObjectByPath(obj, "state/bri"))
      {
         int bri = getIntByPath(obj, "state/bri") / 255.0 * 100.0;
         json_object_set_new(jData, "bri", json_integer(bri));
      }
      if (getObjectByPath(obj, "state/hue"))
      {
         int hue = getIntByPath(obj, "state/hue") / 65535.0 * 360.0;
         json_object_set_new(jData, "hue", json_integer(hue));
      }
      if (getObjectByPath(obj, "state/sat"))
      {
         int sat = getIntByPath(obj, "state/sat") / 255.0 * 100.0;
         json_object_set_new(jData, "sat", json_integer(sat));
      }
      if (getObjectByPath(obj, "state/ct"))
         json_object_set_new(jData, "ct", json_integer(getIntByPath(obj, "state/ct")));
      if (getObjectByPath(obj, "state/colormode"))
         json_object_set_new(jData, "colormode", json_string(getStringByPath(obj, "state/colormode")));
   }

   free(type);

   char* p = json_dumps(jData, JSON_REAL_PRECISION(4));

   {
      cMyMutexLock lock(&messagesInMutex);
      messagesIn.push(p);
   }

   json_decref(jData);
   json_decref(obj);
   free(p);

   return success;
}

//***************************************************************************
// Websocket Client Callback
//***************************************************************************

int Deconz::callbackDeconzWs(lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
   switch (reason)
   {
      case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      {
         tell(eloAlways, "DECONZ: Client connection error '%s'", in ? (char*)in : "(null)");
         connected = false;
         lws_sul_schedule(Deconz::context, 0, &sul, callbackConnect, 5 * LWS_USEC_PER_SEC);
         break;
      }
      case LWS_CALLBACK_CLIENT_ESTABLISHED:
      {
         // push a (re) conect message to call initDevices() again

         tell(eloDebugDeconz, "DECONZ: Established");
         connected = true;
         Deconz::singleton->atInMessage("WS CONNECTED");
         break;
      }
      case LWS_CALLBACK_CLIENT_RECEIVE:
      {
         tell(eloDebugDeconz, "DECONZ: Debug: Rx [%s]", (const char*)in);
         singleton->atInMessage((const char*)in, len);

         break;
      }
      case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
      {
         tell(eloDebugDeconz, "DECONZ: Received pong");
         break;
      }
      case LWS_CALLBACK_CLIENT_CLOSED:
      {
         tell(eloAlways, "DECONZ: Client closed");
         connected = false;
         lws_sul_schedule(Deconz::context, 0, &sul, callbackConnect, 5 * LWS_USEC_PER_SEC);
         break;
      }

      default: break;
   }

   return lws_callback_http_dummy(wsi, reason, user, in, len);
}
