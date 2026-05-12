/*
 * lua.h
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#pragma once

extern "C" {
  #include <lua.h>
  #include <lualib.h>
  #include <lauxlib.h>
}

#include "common.h"
#include "thirdparty/sol/sol.hpp"

//**************************************************************************
// Lua
//**************************************************************************

class Lua
{
   public:

      Lua()             { init(); }
      virtual ~Lua()    { exit(); }

      virtual int init();
      virtual int exit();

      enum Type
      {
         tBoolean,
         tDouble,
         tInteger,
         tString,
         tNil
      };

      struct Result
      {
         Type type;
         bool bValue {false};
         int iValue {0};
         double dValue {0.0};
         std::string sValue;
         bool isNil {false};
      };

      int pushGlobal(const char* name, const char* value);
      int pushGlobal(const char* name, double value);
      int pushGlobal(const char* name, int value);
      int pushGlobal(const char* name, long value);
      int pushGlobal(const char* name, bool value);

      int load(const char* script);
      int syntaxCheck(const char* script, std::string& error);
      int executeFunction(const char* function, const std::vector<std::string>& arguments, Result& res);
      int executeExpression(const char* expression, const std::vector<std::string>& arguments, Result& res, const char* reference = nullptr);

   protected:

      lua_State* handle {};
};

//**************************************************************************
// LuaSol  –  same public API as Lua, but backed by sol3
//
// Usage example:
//
//   LuaSol ls;
//   ls.configure([](sol::state& s) {
//      s.new_usertype<Sensor>("Sensor",
//         "value", sol::readonly(&Sensor::value),
//         "text",  sol::readonly(&Sensor::text));
//   });
//   ls.push([&](sol::state& s) {
//      s["sensors"]   = &sensors;   // map<string, map<int, Sensor>>
//      s["threshold"] = 20.0;
//      s["name"]      = "living";
//   });
//   ls.load(scriptSource);
//   LuaSol::Result res;
//   ls.executeFunction("myFunc", {}, res);
//**************************************************************************

class LuaSol
{
   public:

      LuaSol()          { init(); }
      virtual ~LuaSol() { exit(); }

      virtual int init();
      virtual int exit();

      using Type   = Lua::Type;
      using Result = Lua::Result;

      // Register usertypes / one-time sol::state setup
      int configure(std::function<void(sol::state&)> fn);
      int push(std::function<void(sol::state&)> fn);

      int load(const char* script);
      int syntaxCheck(const char* script, std::string& error);
      int executeFunction(const char* function, const std::vector<std::string>& arguments, Result& res);
      int executeExpression(const char* expression, const std::vector<std::string>& arguments, Result& res, const char* reference = nullptr);

   protected:

      sol::state lua;

   private:

      static void fillResult(sol::object obj, Result& res);
};
