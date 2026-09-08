//***************************************************************************
// Automation Control
// File hasp.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 2026 - Jörg Wendel
//***************************************************************************
//
// openHASP panel support
//
//  The pages of the panel are configured in the WEBIF (setup page 'HASP Panel')
//  and stored in the tables 'hasppages' and 'hasppagewidgets'. From this
//  configuration the jsonl objects are generated and sent line by line via
//  MQTT to <haspMqttTopic>/command/jsonl. openHASP forgets objects received
//  via MQTT on reboot, therefore the pages are sent again whenever the panel
//  publishes 'online' on its LWT topic.
//
//  Layout: a page has up to 4 rows, each row its own number of columns (1..3),
//  stored as json array in hasppages.OPTS: {"layout":[3,2]}
//  Slot (hasppagewidgets.POSITION) = row * 10 + column
//
//  Object id scheme per page (index = row * 3 + column):
//    base = 10 + index * 20
//    base     container (obj)     base+1  title label
//    base+2   main object         base+3  value label
//    base+4.. additional objects (scale labels/ticks, level layers, bulb, ...)
//
//***************************************************************************

#include <jansson.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

#include "lib/json.h"
#include "daemon.h"

//***************************************************************************
// Look (taken from the WEBIF dark theme)
//***************************************************************************

namespace
{
   const int panelWidth {800};
   const int panelHeight {480};
   const int panelMargin {10};
   const int panelGap {10};
   const int maxPages {12};              // HASP_NUM_PAGES of the firmware
   const int maxRows {4};
   const int maxCols {3};
   const int maxLineLength {1900};       // MQTT_MAX_PACKET_SIZE of the firmware is 2048
   const int wtHaspLevel {15};           // widget type id of 'Level' (HASP only, see hasp.js / widgetsetup.js)
   const int maxGlyphSize {120};         // FreeType sbit cache stores the advance in 8 bit, glyphs >= 128px vanish

   const char* colorPage {"#000000"};
   const char* colorCard {"#272727"};
   const char* colorText {"#ffffff"};
   const char* colorDim {"#a3a3a3"};
   const char* colorOn {"#059eeb"};
   const char* colorFill {"#2177AD"};
   const char* colorScale {"#cccccc"};
   const char* colorNeedle {"#f08080"};
   const char* colorCritical {"#ff5757"};
   const char* colorBarBg {"#333333"};

   // openHASP accepts '#rrggbb' and a few color names; the WEBIF stores
   // also 'rgb(...)' values which the panel can't parse

   std::string haspColor(const char* color, const char* def)
   {
      if (isEmpty(color))
         return def;

      if (color[0] == '#')
         return color;

      int r {0}, g {0}, b {0};

      if (sscanf(color, "rgb(%d, %d, %d)", &r, &g, &b) == 3 || sscanf(color, "rgb(%d,%d,%d)", &r, &g, &b) == 3)
      {
         char hex[10];
         snprintf(hex, sizeof(hex), "#%02x%02x%02x", r & 0xff, g & 0xff, b & 0xff);
         return hex;
      }

      return def;
   }

   json_t* haspObj(int page, int id, const char* obj, int parentId, int x, int y, int w, int h)
   {
      json_t* j {json_object()};

      json_object_set_new(j, "page", json_integer(page));
      json_object_set_new(j, "id", json_integer(id));

      if (parentId)
         json_object_set_new(j, "parentid", json_integer(parentId));

      json_object_set_new(j, "obj", json_string(obj));
      json_object_set_new(j, "x", json_integer(x));
      json_object_set_new(j, "y", json_integer(y));
      json_object_set_new(j, "w", json_integer(w));
      json_object_set_new(j, "h", json_integer(h));

      return j;
   }

   json_t* haspLabel(int page, int id, int parentId, int x, int y, int w, int h,
                     const char* text, int font, const char* color, const char* align = nullptr)
   {
      json_t* j {haspObj(page, id, "label", parentId, x, y, w, h)};

      json_object_set_new(j, "text", json_string(text));
      json_object_set_new(j, "text_font", json_integer(font));
      json_object_set_new(j, "text_color", json_string(color));
      json_object_set_new(j, "click", json_false());

      if (align)
         json_object_set_new(j, "align", json_string(align));

      return j;
   }

   void utf8Append(std::string& out, uint32_t cp)
   {
      if (cp < 0x80)
         out += (char)cp;
      else if (cp < 0x800)
      {
         out += (char)(0xC0 | (cp >> 6));
         out += (char)(0x80 | (cp & 0x3F));
      }
      else if (cp < 0x10000)
      {
         out += (char)(0xE0 | (cp >> 12));
         out += (char)(0x80 | ((cp >> 6) & 0x3F));
         out += (char)(0x80 | (cp & 0x3F));
      }
      else
      {
         out += (char)(0xF0 | (cp >> 18));
         out += (char)(0x80 | ((cp >> 12) & 0x3F));
         out += (char)(0x80 | ((cp >> 6) & 0x3F));
         out += (char)(0x80 | (cp & 0x3F));
      }
   }
}

//***************************************************************************
// Widget Types
//***************************************************************************

const char* Daemon::haspWidgetTypeNames[] =
{
   "",
   "Meter",
   "MeterLevel",
   "Value",
   "Text",
   "Symbol",
   "SymbolValue",
   "Time",
   "Level",
   nullptr
};

int Daemon::haspWidgetTypes2Json(json_t* obj)
{
   // the options use the dashboard widget type ids (the WEBIF dialog depends on them),
   // 15 is the HASP only type 'Level'; mapped to HaspWidgetType by haspTypeOfDashboardType()

   json_object_set_new(obj, "Symbol", json_integer(wtSymbol));
   json_object_set_new(obj, "SymbolValue", json_integer(wtSymbolValue));
   json_object_set_new(obj, "Value", json_integer(wtValue));
   json_object_set_new(obj, "Text", json_integer(wtText));
   json_object_set_new(obj, "Meter", json_integer(wtMeter));          // same names and ids as the dashboard
   json_object_set_new(obj, "MeterLevel", json_integer(wtMeterLevel));
   json_object_set_new(obj, "Time", json_integer(wtTime));
   json_object_set_new(obj, "Level", json_integer(wtHaspLevel));

   return done;
}

// map the WEBIF dashboard widget type to the panel widget type
//  (keep in sync with haspTypeOfDashboardType() in htdocs/hasp.js)

Daemon::HaspWidgetType Daemon::haspTypeOfDashboardType(int dashboardWidgetType)
{
   switch (dashboardWidgetType)
   {
      case wtSymbol:        return hwtSymbol;
      case wtSpecialSymbol: return hwtSymbol;
      case wtGauge:         return hwtMeter;
      case wtMeter:         return hwtMeter;
      case wtMeterLevel:    return hwtMeterLevel;
      case wtChartBar:      return hwtMeterLevel;
      case wtText:          return hwtText;
      case wtPlainText:     return hwtText;
      case wtChoice:        return hwtText;
      case wtTime:          return hwtTime;
      case wtSymbolValue:   return hwtSymbolValue;
      case wtSymbolText:    return hwtSymbolValue;
      case wtHaspLevel:     return hwtLevel;
      default:              return hwtValue;
   }
}

//***************************************************************************
// Layout
//   {"layout":[3,2]} -> columns per row; fallback to the rows/cols columns
//***************************************************************************

std::vector<int> Daemon::haspLayoutOf(const char* opts, int rows, int cols)
{
   std::vector<int> layout;
   json_t* jOpts {isEmpty(opts) ? nullptr : jsonLoad(opts, 0, true)};
   json_t* jLayout {jOpts ? json_object_get(jOpts, "layout") : nullptr};

   if (jLayout && json_is_array(jLayout))
   {
      size_t index {0};
      json_t* jCols {};

      json_array_foreach(jLayout, index, jCols)
      {
         if ((int)layout.size() >= maxRows)
            break;

         layout.push_back(std::clamp((int)json_integer_value(jCols), 1, maxCols));
      }
   }

   if (jOpts)
      json_decref(jOpts);

   if (layout.empty())
   {
      rows = std::clamp(rows, 1, maxRows);
      cols = std::clamp(cols, 1, maxCols);

      for (int r = 0; r < rows; r++)
         layout.push_back(cols);
   }

   return layout;
}

//***************************************************************************
// MDI Codepoints
//   parsed from the MDI css of the WEBIF, so the panel uses exactly the
//   same icon set (the complete MDI font has to be uploaded to the panel
//   as /mdi.ttf, see openHASP/README.md)
//***************************************************************************

int Daemon::haspLoadMdiCodepoints()
{
   if (!mdiCodepoints.empty())
      return done;

   char* path {};
   asprintf(&path, "%s/mds/css/materialdesignicons.min.css", httpPath);
   std::ifstream file(path);

   if (!file.is_open())
   {
      tell(eloAlways, "Error: HASP: Can't open '%s' for MDI codepoint lookup", path);
      free(path);
      return fail;
   }

   std::stringstream buffer;
   buffer << file.rdbuf();
   std::string css {buffer.str()};

   // pattern: .mdi-fridge-variant-outline::before{content:"\F15F9"}

   size_t pos {0};

   while ((pos = css.find(".mdi-", pos)) != std::string::npos)
   {
      pos += 5;
      size_t end {css.find_first_of(":,{ ", pos)};

      if (end == std::string::npos)
         break;

      std::string name {css.substr(pos, end-pos)};
      size_t c {css.find("content:\"\\", end)};
      size_t brace {css.find('}', end)};

      if (c == std::string::npos || brace == std::string::npos || c > brace)
         continue;

      c += 10;
      size_t q {css.find('"', c)};

      if (q == std::string::npos)
         break;

      mdiCodepoints[name] = strtoul(css.substr(c, q-c).c_str(), nullptr, 16);
      pos = brace;
   }

   tell(eloDetail, "HASP: Loaded %zu MDI codepoints from '%s'", mdiCodepoints.size(), path);
   free(path);

   return done;
}

//***************************************************************************
// MDI Char
//   symbol like 'mdi:mdi-fridge-outline', 'mdi-fridge-outline' or 'fridge-outline'
//   returns the UTF-8 encoded codepoint for the panel text
//***************************************************************************

std::string Daemon::haspMdiName(const char* symbol)
{
   std::string name {symbol ? symbol : ""};

   if (name.rfind("mdi:", 0) == 0)
      name.erase(0, 4);

   if (name.rfind("mdi-", 0) == 0)
      name.erase(0, 4);

   return name;
}

bool Daemon::haspMdiExists(const std::string& name)
{
   haspLoadMdiCodepoints();
   return !name.empty() && mdiCodepoints.count(name);
}

std::string Daemon::haspMdiChar(const char* symbol)
{
   std::string name {haspMdiName(symbol)};
   std::string out;

   haspLoadMdiCodepoints();

   auto it = mdiCodepoints.find(name);

   if (it == mdiCodepoints.end())
   {
      if (!name.empty())
         tell(eloDetail, "HASP: Unknown MDI symbol '%s', using default", symbol);

      it = mdiCodepoints.find("help-circle-outline");
   }

   if (it != mdiCodepoints.end())
      utf8Append(out, it->second);

   return out;
}

//***************************************************************************
// Widget Defaults
//   1. the same defaults as used for new dashboard widgets (valuefacts)
//   2. overlayed by the options of the widget on the dashboard, if the
//      sensor is configured on one (first found), incl. the mapped widget type
//***************************************************************************

json_t* Daemon::haspWidgetDefaults(const char* type, long address)
{
   json_t* jDefaults {json_object()};

   tableValueFacts->clear();
   tableValueFacts->setValue("TYPE", type);
   tableValueFacts->setValue("ADDRESS", address);

   if (!tableValueFacts->find())
   {
      if (strcmp(type, "TIME") == 0)
      {
         json_object_set_new(jDefaults, "title", json_string("Uhrzeit"));
         json_object_set_new(jDefaults, "widgettype", json_integer(wtTime));
         return jDefaults;
      }

      tell(eloDetail, "HASP: No valuefact for '%s:0x%02lx'", type, address);
      json_object_set_new(jDefaults, "title", json_string(type));
      json_object_set_new(jDefaults, "widgettype", json_integer(wtValue));
      return jDefaults;
   }

   const char* title {!tableValueFacts->getValue("USRTITLE")->isEmpty() ? tableValueFacts->getStrValue("USRTITLE") : tableValueFacts->getStrValue("TITLE")};

   widgetDefaults2Json(jDefaults, tableValueFacts->getStrValue("TYPE"),
                       tableValueFacts->getStrValue("UNIT"), title,
                       tableValueFacts->getIntValue("ADDRESS"));

   json_t* jParameters {jsonLoad(tableValueFacts->getStrValue("PARAMETER"), 0, true)};

   if (jParameters)
   {
      const char* paramId {};
      json_t* jParam {};

      json_object_foreach(jParameters, paramId, jParam)
      {
         if (!json_is_null(jParam))
            json_object_set(jDefaults, paramId, jParam);
      }

      json_decref(jParameters);
   }

   json_object_set_new(jDefaults, "title", json_string(title));
   tableValueFacts->reset();

   // overlay with the dashboard widget of this sensor (if any)

   bool found {false};
   tableDashboards->clear();

   for (int f = selectDashboards->find(); f && !found; f = selectDashboards->fetch())
   {
      tableDashboardWidgets->clear();
      tableDashboardWidgets->setValue("DASHBOARDID", tableDashboards->getIntValue("ID"));

      for (int w = selectDashboardWidgetsFor->find(); w && !found; w = selectDashboardWidgetsFor->fetch())
      {
         if (strcmp(tableDashboardWidgets->getStrValue("TYPE"), type) != 0 || tableDashboardWidgets->getIntValue("ADDRESS") != address)
            continue;

         json_t* jDashOpts {jsonLoad(tableDashboardWidgets->getStrValue("WIDGETOPTS"), 0, true)};

         if (jDashOpts)
         {
            const char* optId {};
            json_t* jOpt {};

            json_object_foreach(jDashOpts, optId, jOpt)
            {
               if (json_is_null(jOpt))
                  continue;

               if (json_is_string(jOpt) && isEmpty(json_string_value(jOpt)))
                  continue;                   // empty strings (e.g. title) don't override the defaults

               json_object_set(jDefaults, optId, jOpt);
            }

            json_decref(jDashOpts);
            found = true;
         }
      }

      selectDashboardWidgetsFor->freeResult();
   }

   selectDashboards->freeResult();

   return jDefaults;
}

//***************************************************************************
// Hasp Pages 2 Json
//   { "pages": { "<id>": { title, order, layout: [3,2], widgets: { "<pos>": { key, ...opts } } } },
//     "widgettypes": { "Meter": 1, ... }, "topic": "hasp/plates" }
//***************************************************************************

int Daemon::haspPages2Json(json_t* obj)
{
   json_t* oPages {json_object()};
   json_object_set_new(obj, "pages", oPages);

   tableHaspPages->clear();

   for (int f = selectHaspPages->find(); f; f = selectHaspPages->fetch())
   {
      json_t* oPage {json_object()};
      char* tmp {};

      asprintf(&tmp, "%ld", tableHaspPages->getIntValue("ID"));
      json_object_set_new(oPages, tmp, oPage);
      free(tmp);

      json_object_set_new(oPage, "title", json_string(tableHaspPages->getStrValue("TITLE")));
      json_object_set_new(oPage, "order", json_integer(tableHaspPages->getIntValue("ORDER")));

      json_t* oLayout {json_array()};

      for (int cols : haspLayoutOf(tableHaspPages->getStrValue("OPTS"), tableHaspPages->getIntValue("ROWS"), tableHaspPages->getIntValue("COLUMNS")))
         json_array_append_new(oLayout, json_integer(cols));

      json_object_set_new(oPage, "layout", oLayout);

      json_t* oWidgets {json_object()};
      json_object_set_new(oPage, "widgets", oWidgets);

      tableHaspPageWidgets->clear();
      tableHaspPageWidgets->setValue("PAGEID", tableHaspPages->getIntValue("ID"));

      for (int w = selectHaspPageWidgetsFor->find(); w; w = selectHaspPageWidgetsFor->fetch())
      {
         json_t* oWidget {jsonLoad(tableHaspPageWidgets->getStrValue("WIDGETOPTS"), 0, true)};

         if (!oWidget)
            oWidget = json_object();

         char* key {};
         asprintf(&key, "%s:0x%02lx", tableHaspPageWidgets->getStrValue("TYPE"), tableHaspPageWidgets->getIntValue("ADDRESS"));
         json_object_set_new(oWidget, "key", json_string(key));
         free(key);

         asprintf(&tmp, "%ld", tableHaspPageWidgets->getIntValue("POSITION"));
         json_object_set_new(oWidgets, tmp, oWidget);
         free(tmp);
      }

      selectHaspPageWidgetsFor->freeResult();
   }

   selectHaspPages->freeResult();

   json_t* oTypes {json_object()};
   haspWidgetTypes2Json(oTypes);
   json_object_set_new(obj, "widgettypes", oTypes);
   json_object_set_new(obj, "topic", json_string(haspMqttTopic.c_str()));

   return done;
}

//***************************************************************************
// Perform Hasp Pages (WEBIF request)
//   { "action": "send" } -> send pages to the panel
//   otherwise the configuration is sent to the client
//***************************************************************************

int Daemon::performHaspPages(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "")};

   if (action == "send")
   {
      int status {haspSendPages()};

      if (status == success)
         return replyResult(success, "Seiten an das Panel gesendet", client);

      return replyResult(fail, status == ignore ? "HASP Panel nicht konfiguriert (haspMqttTopic)" : "Senden fehlgeschlagen, MQTT nicht verbunden?", client);
   }

   json_t* oJson {json_object()};
   haspPages2Json(oJson);
   pushOutMessage(oJson, "hasppages", client);

   return done;
}

//***************************************************************************
// Store Hasp Pages (WEBIF request)
//   { "action": "order", "order": [ "<id>", ... ] }
//   { "<id>": { "action": "delete" } }
//   { "<id>": { title, order, layout: [3,2], widgets: { "<pos>": { key, widgettype, symbol, title } } } }
//   id < 0 -> new page
//***************************************************************************

int Daemon::storeHaspPages(json_t* obj, long client)
{
   std::string action {getStringFromJson(obj, "action", "store")};

   if (action == "order")
   {
      json_t* jOrder {getObjectFromJson(obj, "order")};
      size_t index {0};
      json_t* jId {};

      json_array_foreach(jOrder, index, jId)
      {
         long id {json_is_string(jId) ? atol(json_string_value(jId)) : (long)json_integer_value(jId)};

         tableHaspPages->clear();
         tableHaspPages->setValue("ID", id);

         if (tableHaspPages->find())
         {
            tableHaspPages->setValue("ORDER", (long)index);
            tableHaspPages->store();
         }

         tableHaspPages->reset();
      }
   }
   else
   {
      const char* pageIdStr {};
      json_t* jPage {};

      json_object_foreach(obj, pageIdStr, jPage)
      {
         if (!json_is_object(jPage))
            continue;

         long pageId {atol(pageIdStr)};
         const char* pageAction {getStringFromJson(jPage, "action", "store")};

         if (strcmp(pageAction, "delete") == 0)
         {
            tell(eloDebugWebSock, "Debug: HASP: Deleting page '%ld'", pageId);
            tableHaspPages->deleteWhere("%s = %ld", tableHaspPages->getField("ID")->getDbName(), pageId);
            tableHaspPageWidgets->deleteWhere("%s = %ld", tableHaspPageWidgets->getField("PAGEID")->getDbName(), pageId);
            continue;
         }

         const char* title {getStringFromJson(jPage, "title", "Seite")};

         // layout: columns per row

         std::vector<int> layout;
         json_t* jLayout {getObjectFromJson(jPage, "layout")};

         if (jLayout && json_is_array(jLayout))
         {
            size_t index {0};
            json_t* jCols {};

            json_array_foreach(jLayout, index, jCols)
            {
               if ((int)layout.size() < maxRows)
                  layout.push_back(std::clamp((int)json_integer_value(jCols), 1, maxCols));
            }
         }

         if (layout.empty())
            layout = { 3, 3 };

         int maxColumns {*std::max_element(layout.begin(), layout.end())};
         json_t* jOpts {json_object()};
         json_t* jLayoutOut {json_array()};

         for (int cols : layout)
            json_array_append_new(jLayoutOut, json_integer(cols));

         json_object_set_new(jOpts, "layout", jLayoutOut);
         char* options {json_dumps(jOpts, JSON_COMPACT)};
         json_decref(jOpts);

         if (pageId < 0)
         {
            tableHaspPages->clear();
            tableHaspPages->setValue("TITLE", title);
            tableHaspPages->setValue("COLUMNS", (long)maxColumns);
            tableHaspPages->setValue("ROWS", (long)layout.size());
            tableHaspPages->setValue("ORDER", (long)getIntFromJson(jPage, "order", 99));
            tableHaspPages->setValue("OPTS", options);
            tableHaspPages->store();
            pageId = tableHaspPages->getLastInsertId();
            tell(eloWebSock, "HASP: Created new page '%ld/%s'", pageId, title);
         }

         tableHaspPages->clear();
         tableHaspPages->setValue("ID", pageId);

         if (!tableHaspPages->find())
         {
            tell(eloAlways, "Error: HASP: Storing page '%ld/%s' failed, not found", pageId, title);
            free(options);
            continue;
         }

         tableHaspPages->setValue("TITLE", title);
         tableHaspPages->setValue("COLUMNS", (long)maxColumns);
         tableHaspPages->setValue("ROWS", (long)layout.size());
         tableHaspPages->setValue("ORDER", (long)getIntFromJson(jPage, "order", tableHaspPages->getIntValue("ORDER")));
         tableHaspPages->setValue("OPTS", options);
         tableHaspPages->store();
         tableHaspPages->reset();
         free(options);

         // the widgets

         json_t* jWidgets {getObjectFromJson(jPage, "widgets")};

         if (!jWidgets)
            continue;

         // the options are rebuilt on every store: defaults of the dashboard widget / valuefacts
         // overlayed by the explicit settings of the WEBIF (type, symbol, title)

         tableHaspPageWidgets->deleteWhere("%s = %ld", tableHaspPageWidgets->getField("PAGEID")->getDbName(), pageId);

         const char* posStr {};
         json_t* jWidget {};

         json_object_foreach(jWidgets, posStr, jWidget)
         {
            int pos {atoi(posStr)};
            const char* key {getStringFromJson(jWidget, "key", "")};

            if (isEmpty(key))
               continue;                        // empty slot

            auto tuple = split(key, ':');

            if (tuple.size() < 2)
            {
               tell(eloAlways, "Error: HASP: Ignoring invalid widget key '%s'", key);
               continue;
            }

            long address {strtoll(tuple[1].c_str(), nullptr, 0)};
            json_t* jOptsIn {getObjectFromJson(jWidget, "opts")};
            json_t* jWidgetOpts {};

            // the complete options come from the WEBIF (dialog), seeded there once from the
            // dashboard widget; if missing the daemon seeds them the same way

            if (jOptsIn && json_is_object(jOptsIn) && json_object_size(jOptsIn))
               jWidgetOpts = json_deep_copy(jOptsIn);
            else
               jWidgetOpts = haspWidgetDefaults(tuple[0].c_str(), address);

            char* opts {json_dumps(jWidgetOpts, JSON_REAL_PRECISION(4))};

            tableHaspPageWidgets->clear();
            tableHaspPageWidgets->setValue("PAGEID", pageId);
            tableHaspPageWidgets->setValue("POSITION", (long)pos);
            tableHaspPageWidgets->setValue("TYPE", tuple[0].c_str());
            tableHaspPageWidgets->setValue("ADDRESS", address);
            tableHaspPageWidgets->setValue("WIDGETOPTS", opts);
            tableHaspPageWidgets->store();

            tell(eloDebugWebSock, "Debug: HASP: Page %ld slot %d -> '%s' [%s]", pageId, pos, key, opts);

            free(opts);
            json_decref(jWidgetOpts);
         }
      }
   }

   // publish the new configuration to all clients and the pages to the panel

   json_t* oJson {json_object()};
   haspPages2Json(oJson);
   pushOutMessage(oJson, "hasppages");

   int status {haspSendPages()};

   if (status == fail)
      return replyResult(fail, "Konfiguration gespeichert, Senden an das Panel fehlgeschlagen (MQTT nicht verbunden?)", client);

   return replyResult(success, "Konfiguration gespeichert", client);
}

//***************************************************************************
// Publish
//***************************************************************************

int Daemon::haspPublish(const char* command, const char* payload)
{
   if (haspMqttTopic.empty())
      return ignore;

   if (mqttCheckConnection() != success || !mqttWriter || !mqttWriter->isConnected())
   {
      tell(eloAlways, "Error: HASP: MQTT not connected, can't send '%s'", command);
      return fail;
   }

   std::string topic {haspMqttTopic + "/command/" + command};

   tell(eloMqtt, "-> (%s) [%s]", topic.c_str(), payload);

   return mqttWriter->write(topic.c_str(), payload);
}

int Daemon::haspPublishJsonl(json_t* jObj)
{
   char* line {json_dumps(jObj, JSON_COMPACT | JSON_REAL_PRECISION(2))};
   json_decref(jObj);

   if ((int)strlen(line) > maxLineLength)
      tell(eloAlways, "Warning: HASP: jsonl line exceeds %d bytes, the panel will truncate it [%s]", maxLineLength, line);

   int status {haspPublish("jsonl", line)};
   free(line);

   usleep(3000);   // be gentle with the panel, it handles one message per loop

   return status;
}

//***************************************************************************
// Send Pages
//   generates all configured pages and sends them to the panel
//***************************************************************************

int Daemon::haspSendPages()
{
   if (haspMqttTopic.empty())
      return ignore;

   struct Page { long id; std::string title; std::vector<int> layout; json_t* widgets; };
   std::vector<Page> pages;

   tableHaspPages->clear();

   for (int f = selectHaspPages->find(); f && (int)pages.size() < maxPages; f = selectHaspPages->fetch())
   {
      Page page;

      page.id = tableHaspPages->getIntValue("ID");
      page.title = tableHaspPages->getStrValue("TITLE");
      page.layout = haspLayoutOf(tableHaspPages->getStrValue("OPTS"), tableHaspPages->getIntValue("ROWS"), tableHaspPages->getIntValue("COLUMNS"));
      page.widgets = json_object();

      tableHaspPageWidgets->clear();
      tableHaspPageWidgets->setValue("PAGEID", page.id);

      for (int w = selectHaspPageWidgetsFor->find(); w; w = selectHaspPageWidgetsFor->fetch())
      {
         json_t* oWidget {jsonLoad(tableHaspPageWidgets->getStrValue("WIDGETOPTS"), 0, true)};

         if (!oWidget)
            oWidget = json_object();

         char* key {};
         asprintf(&key, "%s:0x%02lx", tableHaspPageWidgets->getStrValue("TYPE"), tableHaspPageWidgets->getIntValue("ADDRESS"));
         json_object_set_new(oWidget, "key", json_string(key));
         free(key);

         char* pos {};
         asprintf(&pos, "%ld", tableHaspPageWidgets->getIntValue("POSITION"));
         json_object_set_new(page.widgets, pos, oWidget);
         free(pos);
      }

      selectHaspPageWidgetsFor->freeResult();
      pages.push_back(page);
   }

   selectHaspPages->freeResult();

   haspObjects.clear();
   haspLastSent.clear();
   haspPending.clear();
   haspDateLabels.clear();

   int status {haspPublish("clearpage", "all")};

   if (status != success)
   {
      for (auto& page : pages)
         json_decref(page.widgets);

      return status;
   }

   int count {(int)pages.size()};

   for (int i = 0; i < count; i++)
   {
      int pageNo {i+1};
      int next {i+1 < count ? pageNo+1 : 1};
      int prev {i > 0 ? pageNo-1 : count};

      haspBuildPage(pageNo, pages[i].layout, pages[i].widgets, next, prev);
      json_decref(pages[i].widgets);
   }

   if (count)
      haspPublish("page", "1");

   tell(eloAlways, "HASP: Sent %d pages with %zu sensor widgets to '%s'", count, haspObjects.size(), haspMqttTopic.c_str());

   haspPublishAllValues();

   return success;
}

//***************************************************************************
// Build Page
//***************************************************************************

int Daemon::haspBuildPage(int pageNo, const std::vector<int>& layout, json_t* jWidgets, int nextPage, int prevPage)
{
   // the page itself: black, swipe left/right to change the page

   json_t* jPage {json_object()};

   json_object_set_new(jPage, "page", json_integer(pageNo));
   json_object_set_new(jPage, "id", json_integer(0));
   json_object_set_new(jPage, "bg_color", json_string(colorPage));
   json_object_set_new(jPage, "bg_grad_color", json_string(colorPage));
   json_object_set_new(jPage, "bg_grad_dir", json_integer(0));
   json_object_set_new(jPage, "swipe", json_true());
   json_object_set_new(jPage, "next", json_integer(nextPage));
   json_object_set_new(jPage, "prev", json_integer(prevPage));

   haspPublishJsonl(jPage);

   // the cards, each row with its own number of columns

   int rows {(int)layout.size()};
   int cardHeight {(panelHeight - 2*panelMargin - (rows-1)*panelGap) / rows};

   for (int row = 0; row < rows; row++)
   {
      int cols {layout[row]};
      int cardWidth {(panelWidth - 2*panelMargin - (cols-1)*panelGap) / cols};

      for (int col = 0; col < cols; col++)
      {
         char posStr[16];
         snprintf(posStr, sizeof(posStr), "%d", row * 10 + col);
         json_t* jOpts {json_object_get(jWidgets, posStr)};

         if (!jOpts)
            continue;

         int x {panelMargin + col * (cardWidth + panelGap)};
         int y {panelMargin + row * (cardHeight + panelGap)};

         haspBuildWidget(pageNo, row, col, x, y, cardWidth, cardHeight, getStringFromJson(jOpts, "key", ""), jOpts);
      }
   }

   return done;
}

//***************************************************************************
// Build Widget (one card)
//***************************************************************************

int Daemon::haspBuildWidget(int pageNo, int row, int col, int x, int y, int w, int h, const char* key, json_t* jOpts)
{
   int base {10 + (row * maxCols + col) * 20};    // up to 20 object ids per card (max 12 cards -> id 250)
   bool small {h < 160};                     // 4 rows -> small fonts
   int titleFont {small ? 16 : 24};
   int valueFont {small ? 24 : 32};
   int smallFont {small ? 12 : 16};
   HaspWidgetType widgetType {haspTypeOfDashboardType(getIntFromJson(jOpts, "widgettype", wtValue))};
   int titleHeight {widgetType == hwtTime ? 0 : titleFont + 14};   // enough for the font incl. line spacing, otherwise 'dots' truncates

   // the title: the dialog stores an empty title if the user didn't set one -> title of the valuefact

   std::string titleStr {getStringFromJson(jOpts, "title", "")};

   if (titleStr.empty())
   {
      auto tuple = split(key, ':');

      tableValueFacts->clear();
      tableValueFacts->setValue("TYPE", tuple[0].c_str());
      tableValueFacts->setValue("ADDRESS", tuple.size() > 1 ? strtol(tuple[1].c_str(), nullptr, 0) : 0L);

      if (tableValueFacts->find())
         titleStr = !tableValueFacts->getValue("USRTITLE")->isEmpty() ? tableValueFacts->getStrValue("USRTITLE") : tableValueFacts->getStrValue("TITLE");
      else
         titleStr = key;

      tableValueFacts->reset();
   }

   const char* title {titleStr.c_str()};
   const char* unit {getStringFromJson(jOpts, "unit", "")};
   int scaleMin {getIntFromJson(jOpts, "scalemin", 0)};
   int scaleMax {getIntFromJson(jOpts, "scalemax", 100)};
   int critMax {getIntFromJson(jOpts, "critmax", scaleMax)};

   if (scaleMax <= scaleMin)
      scaleMax = scaleMin + 100;

   std::string valueText {"--"};

   if (!isEmpty(unit))
      valueText += std::string(" ") + unit;

   // container and title

   json_t* jCard {haspObj(pageNo, base, "obj", 0, x, y, w, h)};

   json_object_set_new(jCard, "bg_color", json_string(colorCard));
   json_object_set_new(jCard, "bg_grad_color", json_string(colorCard));
   json_object_set_new(jCard, "bg_grad_dir", json_integer(0));
   json_object_set_new(jCard, "radius", json_integer(3));
   json_object_set_new(jCard, "border_width", json_integer(0));
   json_object_set_new(jCard, "click", json_false());
   haspPublishJsonl(jCard);

   if (widgetType != hwtTime)
   {
      json_t* jTitle {haspLabel(pageNo, base+1, base, 8, 4, w-16, titleHeight, title, titleFont, colorText)};
      json_object_set_new(jTitle, "pad_top", json_integer(0));
      json_object_set_new(jTitle, "pad_bottom", json_integer(0));
      json_object_set_new(jTitle, "mode", json_string("dots"));
      haspPublishJsonl(jTitle);
   }

   // inner area below the title

   int ix {8};
   int iy {titleHeight + 6};
   int iw {w - 16};
   int ih {h - iy - 8};

   HaspObjRef ref;
   ref.page = pageNo;
   ref.objId = base + 2;
   ref.widgetType = widgetType;
   ref.unit = unit;
   ref.symbolOff = haspMdiChar(getStringFromJson(jOpts, "symbol", ""));
   ref.symbolOn = haspMdiChar(getStringFromJson(jOpts, "symbolOn", getStringFromJson(jOpts, "symbol", "")));
   ref.colorOff = haspColor(getStringFromJson(jOpts, "color", ""), colorText);
   ref.colorOn = haspColor(getStringFromJson(jOpts, "colorOn", ""), colorOn);
   ref.scaleMin = scaleMin;
   ref.scaleMax = scaleMax;

   if (strcmp(unit, "%") == 0)             // percent values: fixed range
   {
      ref.scaleMin = 0;
      ref.scaleMax = 100;
   }

   switch (widgetType)
   {
      case hwtMeter:
      {
         int size {std::min(iw, ih)};
         json_t* jGauge {haspObj(pageNo, base+2, "gauge", base, (w - size) / 2, iy, size, size)};

         json_object_set_new(jGauge, "min", json_integer(scaleMin));
         json_object_set_new(jGauge, "max", json_integer(scaleMax));
         json_object_set_new(jGauge, "val", json_integer(scaleMin));
         json_object_set_new(jGauge, "critical_value", json_integer(critMax));
         json_object_set_new(jGauge, "angle", json_integer(240));
         json_object_set_new(jGauge, "line_count", json_integer(small ? 16 : 21));
         json_object_set_new(jGauge, "label_count", json_integer(small ? 3 : 5));
         json_object_set_new(jGauge, "bg_opa", json_integer(0));
         json_object_set_new(jGauge, "border_width", json_integer(0));
         json_object_set_new(jGauge, "click", json_false());
         json_object_set_new(jGauge, "line_color", json_string(colorScale));
         json_object_set_new(jGauge, "scale_grad_color", json_string(colorScale));
         json_object_set_new(jGauge, "scale_end_color", json_string(colorCritical));
         json_object_set_new(jGauge, "line_color20", json_string(colorScale));
         json_object_set_new(jGauge, "scale_grad_color20", json_string(colorScale));
         json_object_set_new(jGauge, "scale_end_color20", json_string(colorCritical));
         json_object_set_new(jGauge, "text_color20", json_string(colorText));
         json_object_set_new(jGauge, "text_font20", json_integer(12));
         std::string needle {haspColor(getStringFromJson(jOpts, "color", ""), colorNeedle)};
         json_object_set_new(jGauge, "line_color10", json_string(needle.c_str()));
         json_object_set_new(jGauge, "line_width10", json_integer(small ? 3 : 4));
         json_object_set_new(jGauge, "bg_color10", json_string(needle.c_str()));
         haspPublishJsonl(jGauge);

         // the value in the gap of the 240° scale at the bottom of the gauge
         haspPublishJsonl(haspLabel(pageNo, base+3, base, ix, iy + size - valueFont - 6, iw, valueFont + 6, valueText.c_str(), valueFont, colorText, "center"));
         ref.valueId = base + 3;
         break;
      }

      case hwtMeterLevel:
      {
         // vertical bar (LVGL: h > w) growing from the bottom, 3 scale marks with labels
         // to the right, value on the right half; temperatures get a bulb -> thermometer

         bool thermometer {strstr(unit, "°") != nullptr};
         int barWidth {small ? 16 : 22};
         int bulb {thermometer ? barWidth * 7 / 5 : 0};
         int barX {ix + 12};
         int barY {iy + 4};
         int barHeight {ih - 8 - (thermometer ? bulb / 2 + 2 : 0)};
         std::string fillColor {haspColor(getStringFromJson(jOpts, "barcolor", ""), thermometer ? colorCritical : colorFill)};
         const char* fill {fillColor.c_str()};

         json_t* jBar {haspObj(pageNo, base+2, "bar", base, barX, barY, barWidth, barHeight)};

         json_object_set_new(jBar, "min", json_integer(scaleMin));
         json_object_set_new(jBar, "max", json_integer(scaleMax));
         json_object_set_new(jBar, "val", json_integer(scaleMin));
         json_object_set_new(jBar, "anim_time", json_integer(500));
         json_object_set_new(jBar, "click", json_false());
         json_object_set_new(jBar, "bg_color", json_string(colorBarBg));
         json_object_set_new(jBar, "bg_grad_color", json_string(colorBarBg));
         json_object_set_new(jBar, "bg_grad_dir", json_integer(0));
         json_object_set_new(jBar, "border_width", json_integer(2));
         json_object_set_new(jBar, "border_color", json_string("#545454"));
         json_object_set_new(jBar, "radius", json_integer(barWidth / 2));
         json_object_set_new(jBar, "bg_color10", json_string(fill));
         json_object_set_new(jBar, "bg_grad_color10", json_string(fill));
         json_object_set_new(jBar, "bg_grad_dir10", json_integer(0));
         json_object_set_new(jBar, "radius10", json_integer(barWidth / 2));
         haspPublishJsonl(jBar);

         if (thermometer)
         {
            json_t* jBulb {haspObj(pageNo, base+7, "obj", base, barX + barWidth/2 - bulb/2, barY + barHeight - bulb/2 - 2, bulb, bulb)};
            json_object_set_new(jBulb, "bg_color", json_string(fill));
            json_object_set_new(jBulb, "bg_grad_color", json_string(fill));
            json_object_set_new(jBulb, "bg_grad_dir", json_integer(0));
            json_object_set_new(jBulb, "border_width", json_integer(2));
            json_object_set_new(jBulb, "border_color", json_string("#545454"));
            json_object_set_new(jBulb, "radius", json_integer(bulb / 2));
            json_object_set_new(jBulb, "click", json_false());
            haspPublishJsonl(jBulb);
         }

         // scale: min (bottom), mid, max (top) with tick marks

         int scaleX {barX + barWidth + 4};
         int labelWidth {small ? 40 : 56};
         int marks[3] {scaleMax, (scaleMin + scaleMax) / 2, scaleMin};

         for (int i = 0; i < 3; i++)
         {
            int yMark {barY + i * (barHeight - 1) / 2};
            char text[30];

            json_t* jTick {haspObj(pageNo, base+8+i, "obj", base, scaleX, yMark - 1, 6, 2)};
            json_object_set_new(jTick, "bg_color", json_string(colorScale));
            json_object_set_new(jTick, "bg_grad_color", json_string(colorScale));
            json_object_set_new(jTick, "bg_grad_dir", json_integer(0));
            json_object_set_new(jTick, "border_width", json_integer(0));
            json_object_set_new(jTick, "radius", json_integer(0));
            json_object_set_new(jTick, "click", json_false());
            haspPublishJsonl(jTick);

            snprintf(text, sizeof(text), "%d", marks[i]);
            haspPublishJsonl(haspLabel(pageNo, base+4+i, base, scaleX + 8, yMark - (smallFont + 4) / 2, labelWidth, smallFont + 4, text, smallFont, colorDim, "left"));
         }

         // the value on the right half

         int valueX {scaleX + 8 + labelWidth + 4};
         haspPublishJsonl(haspLabel(pageNo, base+3, base, valueX, iy + (ih - valueFont - 6) / 2, w - 8 - valueX, valueFont + 6, valueText.c_str(), valueFont, colorText, "center"));
         ref.valueId = base + 3;
         break;
      }

      case hwtText:
      {
         json_t* jText {haspLabel(pageNo, base+2, base, ix, iy + 4, iw, ih - 8, "--", titleFont, colorText)};
         json_object_set_new(jText, "mode", json_string("break"));
         haspPublishJsonl(jText);
         ref.valueId = base + 2;
         break;
      }

      case hwtSymbol:
      {
         // the symbol itself is the (toggle) button, color shows the state

         int size {std::min(iw, ih)};
         json_t* jBtn {haspObj(pageNo, base+2, "btn", base, (w - size) / 2, iy + (ih - size) / 2, size, size)};
         char font[20];

         snprintf(font, sizeof(font), "mdi_%d", std::min(size - 16, maxGlyphSize));

         json_object_set_new(jBtn, "toggle", json_true());
         json_object_set_new(jBtn, "val", json_integer(0));
         json_object_set_new(jBtn, "text", json_string(ref.symbolOff.c_str()));
         json_object_set_new(jBtn, "text_font", json_string(font));
         json_object_set_new(jBtn, "text_color", json_string(ref.colorOff.c_str()));
         json_object_set_new(jBtn, "text_color01", json_string(ref.colorOn.c_str()));
         json_object_set_new(jBtn, "text_color02", json_string(colorDim));
         json_object_set_new(jBtn, "text_color03", json_string(colorFill));
         json_object_set_new(jBtn, "bg_opa", json_integer(0));
         json_object_set_new(jBtn, "bg_opa01", json_integer(0));
         json_object_set_new(jBtn, "bg_opa02", json_integer(0));
         json_object_set_new(jBtn, "bg_opa03", json_integer(0));
         json_object_set_new(jBtn, "border_width", json_integer(0));
         json_object_set_new(jBtn, "outline_width", json_integer(0));
         json_object_set_new(jBtn, "shadow_width", json_integer(0));
         haspPublishJsonl(jBtn);
         break;
      }

      case hwtSymbolValue:
      {
         int size {std::min(ih, iw / 2)};
         char font[20];

         snprintf(font, sizeof(font), "mdi_%d", std::min(size - 8, maxGlyphSize));

         json_t* jSym {haspLabel(pageNo, base+2, base, ix, iy + (ih - size) / 2, size, size, ref.symbolOff.c_str(), 0, ref.colorOff.c_str(), "center")};
         json_object_set_new(jSym, "text_font", json_string(font));
         haspPublishJsonl(jSym);

         haspPublishJsonl(haspLabel(pageNo, base+3, base, ix + size + 6, iy + (ih - valueFont - 6) / 2, iw - size - 6, valueFont + 6, valueText.c_str(), valueFont, colorText, "center"));
         ref.valueId = base + 3;
         break;
      }

      case hwtTime:
      {
         // clock and date, rendered and refreshed by the panel itself (label template = strftime)

         int clockFont {small ? 40 : 72};
         int dateFont {small ? 16 : 24};
         json_t* jClock {haspLabel(pageNo, base+2, base, ix, iy + (ih - clockFont - dateFont - 20) / 2, iw, clockFont + 12, "--:--", clockFont, colorText, "center")};
         json_object_set_new(jClock, "template", json_string("%H:%M"));
         haspPublishJsonl(jClock);

         // the date: no strftime template, the firmware knows only english weekday names,
         // the text is set by haspPublishAllValues() (german weekday)

         haspPublishJsonl(haspLabel(pageNo, base+3, base, ix, iy + (ih - clockFont - dateFont - 20) / 2 + clockFont + 16, iw, dateFont + 8, "", dateFont, colorDim, "center"));
         haspDateLabels.push_back({ pageNo, base+3 });
         break;
      }

      case hwtLevel:
      {
         // three layers: solid glyph dark gray (body), solid glyph in fill color clipped by a
         // container growing from the bottom (fill), outline glyph on top (frame).
         // MDI has most symbols as pair 'name' / 'name-outline', we derive the other variant.

         std::string name {haspMdiName(getStringFromJson(jOpts, "symbol", ""))};
         std::string solid {name};
         std::string outline;

         if (name.size() > 8 && name.compare(name.size()-8, 8, "-outline") == 0)
         {
            outline = name;
            solid = name.substr(0, name.size()-8);

            if (!haspMdiExists(solid))
               solid = name;
         }
         else if (haspMdiExists(name + "-outline"))
            outline = name + "-outline";

         std::string solidChar {haspMdiChar(solid.c_str())};
         std::string outlineChar {outline.empty() || outline == solid ? "" : haspMdiChar(outline.c_str())};

         int size {std::min({ih, iw / 2, maxGlyphSize + 4})};
         int top {iy + (ih - size) / 2};
         char font[20];

         snprintf(font, sizeof(font), "mdi_%d", size - 4);

         json_t* jBody {haspLabel(pageNo, base+4, base, ix, top, size, size, solidChar.c_str(), 0, "#3a3a3a", "center")};
         json_object_set_new(jBody, "text_font", json_string(font));
         haspPublishJsonl(jBody);

         json_t* jClip {haspObj(pageNo, base+2, "obj", base, ix, top + size, size, 1)};
         json_object_set_new(jClip, "bg_opa", json_integer(0));
         json_object_set_new(jClip, "border_width", json_integer(0));
         json_object_set_new(jClip, "radius", json_integer(0));
         json_object_set_new(jClip, "click", json_false());
         haspPublishJsonl(jClip);

         std::string levelColor {haspColor(getStringFromJson(jOpts, "barcolor", ""), ref.colorOn.c_str())};
         json_t* jFill {haspLabel(pageNo, base+5, base+2, 0, -size, size, size, solidChar.c_str(), 0, levelColor.c_str(), "center")};
         json_object_set_new(jFill, "text_font", json_string(font));
         haspPublishJsonl(jFill);

         if (!outlineChar.empty())
         {
            json_t* jFrame {haspLabel(pageNo, base+7, base, ix, top, size, size, outlineChar.c_str(), 0, "#707070", "center")};
            json_object_set_new(jFrame, "text_font", json_string(font));
            haspPublishJsonl(jFrame);
         }

         haspPublishJsonl(haspLabel(pageNo, base+3, base, ix + size + 6, iy + (ih - valueFont - smallFont - 12) / 2, iw - size - 6, valueFont + 6, valueText.c_str(), valueFont, colorText, "center"));
         haspPublishJsonl(haspLabel(pageNo, base+6, base, ix + size + 6, iy + (ih - valueFont - smallFont - 12) / 2 + valueFont + 8, iw - size - 6, smallFont + 6, "-- %", smallFont, colorDim, "center"));

         ref.valueId = base + 3;
         ref.extraId = base + 6;
         ref.size = size;
         ref.top = top;
         break;
      }

      case hwtValue:
      default:
      {
         int valueY {iy + (ih - valueFont - smallFont - 14) / 2};
         haspPublishJsonl(haspLabel(pageNo, base+2, base, ix, valueY, iw, valueFont + 8, "--", valueFont, colorText, "center"));
         haspPublishJsonl(haspLabel(pageNo, base+3, base, ix, valueY + valueFont + 8, iw, smallFont + 6, unit, smallFont, colorDim, "center"));
         ref.valueId = base + 2;
         break;
      }
   }

   if (widgetType != hwtTime)
      haspObjects[key].push_back(ref);

   return done;
}

//***************************************************************************
// Values
//   called for every sensor change (via mqttHaPublish) and after sending
//   the pages; commands are batched into 'json' messages:
//   ["p1b12.val=23","p1b13.text=23.4 °C", ...]
//***************************************************************************

namespace
{
   std::string haspFormatValue(double value)
   {
      char buf[50];

      if (fabs(value - round(value)) < 0.05 || fabs(value) >= 1000)
         snprintf(buf, sizeof(buf), "%.0f", value);
      else
         snprintf(buf, sizeof(buf), "%.1f", value);

      return buf;
   }
}

void Daemon::haspQueueCommand(const std::string& objAttr, const std::string& value)
{
   auto it = haspLastSent.find(objAttr);

   if (it != haspLastSent.end() && it->second == value)
      return;                                   // unchanged

   haspLastSent[objAttr] = value;
   haspPending.push_back(objAttr + "=" + value);
}

int Daemon::haspFlushCommands()
{
   int status {success};

   while (!haspPending.empty())
   {
      json_t* jArray {json_array()};
      size_t size {2};

      while (!haspPending.empty() && size + haspPending.front().size() + 3 < (size_t)maxLineLength)
      {
         size += haspPending.front().size() + 3;
         json_array_append_new(jArray, json_string(haspPending.front().c_str()));
         haspPending.erase(haspPending.begin());
      }

      if (!json_array_size(jArray))
      {
         tell(eloAlways, "Warning: HASP: Dropping oversized command [%s]", haspPending.front().c_str());
         haspPending.erase(haspPending.begin());
         json_decref(jArray);
         continue;
      }

      char* payload {json_dumps(jArray, JSON_COMPACT)};
      json_decref(jArray);
      status = haspPublish("json", payload);
      free(payload);

      if (status != success)
      {
         haspPending.clear();
         return status;
      }
   }

   return status;
}

int Daemon::haspPublishSensor(const SensorData& sensor)
{
   if (haspMqttTopic.empty() || haspObjects.empty())
      return ignore;

   char* key {};
   asprintf(&key, "%s:0x%02x", sensor.type.c_str(), sensor.address);
   auto it = haspObjects.find(key);
   free(key);

   if (it == haspObjects.end())
      return ignore;

   bool isStatus {sensor.kind == "status"};
   bool on {isStatus ? sensor.state : sensor.value != 0.0};
   std::string number {haspFormatValue(isStatus ? (double)sensor.state : sensor.value)};
   char obj[30];

   for (const auto& ref : it->second)
   {
      std::string withUnit {number + (ref.unit.empty() ? "" : " " + ref.unit)};

      switch (ref.widgetType)
      {
         case hwtMeter:
         case hwtMeterLevel:
            snprintf(obj, sizeof(obj), "p%db%d.val", ref.page, ref.objId);
            haspQueueCommand(obj, std::to_string((long)lround(sensor.value)));
            snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.valueId);
            haspQueueCommand(obj, withUnit);
            break;

         case hwtValue:
            snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.valueId);
            haspQueueCommand(obj, sensor.kind == "text" && !sensor.text.empty() ? sensor.text : number);
            break;

         case hwtText:
            snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.valueId);
            haspQueueCommand(obj, !sensor.text.empty() ? sensor.text : withUnit);
            break;

         case hwtSymbol:
            snprintf(obj, sizeof(obj), "p%db%d.val", ref.page, ref.objId);
            haspQueueCommand(obj, on ? "1" : "0");

            if (ref.symbolOn != ref.symbolOff)
            {
               snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.objId);
               haspQueueCommand(obj, on ? ref.symbolOn : ref.symbolOff);
            }
            break;

         case hwtLevel:
         {
            double range {ref.scaleMax - ref.scaleMin};
            double pct {range > 0 ? (sensor.value - ref.scaleMin) / range : 0};
            pct = std::clamp(pct, 0.0, 1.0);
            int fill {(int)lround(ref.size * pct)};

            snprintf(obj, sizeof(obj), "p%db%d.y", ref.page, ref.objId);
            haspQueueCommand(obj, std::to_string(ref.top + ref.size - fill));
            snprintf(obj, sizeof(obj), "p%db%d.h", ref.page, ref.objId);
            haspQueueCommand(obj, std::to_string(std::max(fill, 1)));
            snprintf(obj, sizeof(obj), "p%db%d.y", ref.page, ref.objId + 3);      // the fill glyph inside the clip container
            haspQueueCommand(obj, std::to_string(-(ref.size - fill)));
            snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.valueId);
            haspQueueCommand(obj, withUnit);
            snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.extraId);
            haspQueueCommand(obj, std::to_string((int)lround(pct * 100)) + " %");
            break;
         }

         case hwtSymbolValue:
            snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.valueId);
            haspQueueCommand(obj, isStatus ? std::string(on ? "An" : "Aus") : withUnit);

            if (isStatus)
            {
               snprintf(obj, sizeof(obj), "p%db%d.text_color", ref.page, ref.objId);
               haspQueueCommand(obj, on ? ref.colorOn : ref.colorOff);

               if (ref.symbolOn != ref.symbolOff)
               {
                  snprintf(obj, sizeof(obj), "p%db%d.text", ref.page, ref.objId);
                  haspQueueCommand(obj, on ? ref.symbolOn : ref.symbolOff);
               }
            }
            break;

         default:
            break;
      }
   }

   return haspFlushCommands();
}

int Daemon::haspPublishAllValues()
{
   if (haspMqttTopic.empty())
      return ignore;

   int count {0};

   for (const auto& it : haspObjects)
   {
      auto tuple = split(it.first, ':');

      if (tuple.size() < 2)
         continue;

      int address {(int)strtoul(tuple[1].c_str(), nullptr, 0)};

      if (!sensors.count(tuple[0]) || !sensors[tuple[0]].count(address))
         continue;

      const SensorData& sensor {sensors[tuple[0]][address]};

      if (!sensor.valid && !sensor.last && sensor.kind != "status")
         continue;                              // never got a value yet

      haspPublishSensor(sensor);
      count++;
   }

   // date of the Time widgets (german weekday)

   if (!haspDateLabels.empty())
   {
      static const char* weekdays[] { "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag" };
      time_t now {time(0)};
      tm* t {localtime(&now)};
      char date[60];
      char obj[30];

      snprintf(date, sizeof(date), "%s, %02d.%02d.%04d", weekdays[t->tm_wday], t->tm_mday, t->tm_mon+1, t->tm_year+1900);

      for (const auto& label : haspDateLabels)
      {
         snprintf(obj, sizeof(obj), "p%db%d.text", label.first, label.second);
         haspQueueCommand(obj, date);
      }

      haspFlushCommands();
   }

   if (count && !haspLastSent.empty())
      tell(eloDebug, "HASP: Checked values of %d sensors", count);

   return done;
}

//***************************************************************************
// Dispatch State (touch events of the panel)
//   topic:   hasp/<hostname>/state/p<page>b<id>
//   payload: {"event":"up","val":1}  (toggle buttons deliver the new value with 'up')
//   Only the Symbol widgets are buttons; controllable sensors are switched via toggleIo(),
//   for all others the button state is reverted to the sensor state.
//***************************************************************************

int Daemon::haspDispatchState(const char* topic, const char* message)
{
   const char* obj {strrchr(topic, '/')};
   int page {0};
   int id {0};

   if (!obj || sscanf(obj+1, "p%db%d", &page, &id) != 2)
      return ignore;                       // statusupdate, sensors, idle, ...

   json_t* jData {jsonLoad(message, 0, true)};

   if (!jData)
      return ignore;

   std::string event {getStringFromJson(jData, "event", "")};
   int val {getIntFromJson(jData, "val", na)};
   json_decref(jData);

   if (event != "up")
      return done;

   for (const auto& it : haspObjects)
   {
      for (const auto& ref : it.second)
      {
         if (ref.page != page || ref.objId != id || ref.widgetType != hwtSymbol)
            continue;

         auto tuple = split(it.first, ':');

         if (tuple.size() < 2)
            return fail;

         std::string type {tuple[0]};
         uint address {(uint)strtoul(tuple[1].c_str(), nullptr, 0)};

         bool controllable {type == "DO" || type == "SC" || type == "DZL" || type == "DZLG" || type == "HMB" ||
                            (type == "GPIO" && sensors.count(type) && sensors[type].count(address) && sensors[type][address].fct == "out")};

         if (controllable && val != na)
         {
            tell(eloAlways, "HASP: Touch on '%s' (p%db%d) -> switch to %d", it.first.c_str(), page, id, val);
            toggleIo(address, type.c_str(), val);
         }
         else
         {
            // not switchable: revert the button to the state of the sensor

            tell(eloDetail, "HASP: Touch on '%s' (p%db%d) ignored, sensor not controllable", it.first.c_str(), page, id);

            char attr[30];
            snprintf(attr, sizeof(attr), "p%db%d.val", page, id);
            haspLastSent.erase(attr);

            if (sensors.count(type) && sensors[type].count(address))
               haspPublishSensor(sensors[type][address]);
         }

         return done;
      }
   }

   return done;
}
