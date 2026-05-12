/*
 * lua.c
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "lua.h"

//**************************************************************************
// Init / Exit
//**************************************************************************

int Lua::init()
{
   handle = luaL_newstate();
   luaL_openlibs(handle);

/*   const char* file = "foobar.lua";  // #TODO

   tell(eloDebug, "Debug: Loading LUA file '%s'", file);

   if (luaL_loadfile(handle, file) != LUA_OK)
   {
      tell(eloAlways, "Error: '%s'\n", lua_tostring(handle, lua_gettop(handle)));
      lua_pop(handle, lua_gettop(handle));
      return fail;
      } */

   return success;
}

int Lua::exit()
{
   if (handle)
      lua_close(handle);
   handle = nullptr;
   return success;
}

//**************************************************************************
//
//**************************************************************************

int Lua::pushGlobal(const char* name, const char* value)
{
   lua_pushstring(handle, value);
   lua_setglobal(handle, name);
   return done;
}

int Lua::pushGlobal(const char* name, double value)
{
   lua_pushnumber(handle, value);
   lua_setglobal(handle, name);
   return done;
}

int Lua::pushGlobal(const char* name, int value)
{
   lua_pushinteger(handle, value);
   lua_setglobal(handle, name);
   return done;
}

int Lua::pushGlobal(const char* name, long value)
{
   lua_pushinteger(handle, value);
   lua_setglobal(handle, name);
   return done;
}

int Lua::pushGlobal(const char* name, bool value)
{
   lua_pushboolean(handle, value);
   lua_setglobal(handle, name);
   return done;
}

//**************************************************************************
// Load Script (defines functions for later executeFunction calls)
//**************************************************************************

int Lua::load(const char* script)
{
   if (luaL_dostring(handle, script) != LUA_OK)
   {
      tell(eloAlways, "Error: Lua load: '%s'", lua_tostring(handle, -1));
      lua_pop(handle, 1);
      return fail;
   }

   return success;
}

//**************************************************************************
// Syntax Check (compile only, do not execute)
//**************************************************************************

int Lua::syntaxCheck(const char* script, std::string& error)
{
   char* code {};
   asprintf(&code, "function execute()\n%s\nend", script);

   int rc = luaL_loadstring(handle, code);
   free(code);

   if (rc != LUA_OK)
   {
      error = lua_tostring(handle, -1);
      lua_pop(handle, 1);
      return fail;
   }

   lua_pop(handle, 1);   // pop the compiled chunk
   return success;
}

//**************************************************************************
// Execute Script
//**************************************************************************

int Lua::executeFunction(const char* function, const std::vector<std::string>& arguments, Result& res)
{
   res = {};

   lua_getglobal(handle, function);

   if (!lua_isfunction(handle, -1))
   {
      tell(eloAlways, "Error: Function '%s' not found", function);
      lua_pop(handle, lua_gettop(handle));
      return fail;
   }

   for (const auto& arg : arguments)
   {
      tell(eloLua, "LUA: Add parameter '%s'", arg.c_str());
      lua_pushstring(handle, arg.c_str());
   }

   if (lua_pcall(handle, arguments.size(), 1, 0) == LUA_OK)
   {
      if (lua_isboolean(handle, -1))
      {
         res.type = tBoolean;
         res.bValue = lua_toboolean(handle, -1);
      }
      else if (lua_isinteger(handle, -1))
      {
         res.type = tInteger;
         res.iValue = lua_tointeger(handle, -1);
      }
      else if (lua_isnumber(handle, -1))
      {
         res.type = tDouble;
         res.dValue = lua_tonumber(handle, -1);
      }
      else if (lua_isstring(handle, -1))
      {
         res.type = tString;
         res.sValue = lua_tostring(handle, -1);
      }
      else
      {
         res.type = tNil;
         res.isNil = true;
      }

      lua_pop(handle, 1);                      // pop return value from stack
      lua_pop(handle, lua_gettop(handle));     // pop function from stack
   }
   else
   {
      tell(eloAlways, "Error: '%s' #1\n", lua_tostring(handle, lua_gettop(handle)));
      lua_pop(handle, lua_gettop(handle));
      return fail;
   }

   return success;
}


//**************************************************************************
// Execute LUA String
//**************************************************************************

int Lua::executeExpression(const char* expression, const std::vector<std::string>& arguments, Result& res)
{
   res = {};

   char* code {};

   asprintf(&code, "function execute()"          \
            "  %s\n"                             \
            "end", expression);

   tell(eloLua, "LUA: Calling '%.20s ..'", code);

   if (luaL_loadstring(handle, code) == LUA_OK)
   {
      free(code);

      if (lua_pcall(handle, 0, 0, 0) != LUA_OK)
      {
         tell(eloAlways, "Error: Lua load execute: '%s'", lua_tostring(handle, -1));
         lua_pop(handle, lua_gettop(handle));
         return fail;
      }

      lua_getglobal(handle, "execute");

      if (!lua_isfunction(handle, -1))
      {
         tell(eloAlways, "Error: in '%s'", lua_tostring(handle, lua_gettop(handle)));
         lua_pop(handle, lua_gettop(handle));
         return fail;
      }

      for (const auto& arg : arguments)
         lua_pushstring(handle, arg.c_str());

      if (lua_pcall(handle, arguments.size(), 1, 0) == LUA_OK)
      {
         if (lua_isboolean(handle, -1))
         {
            res.type = tBoolean;
            res.bValue = lua_toboolean(handle, -1);
         }
         else if (lua_isinteger(handle, -1))
         {
            res.type = tInteger;
            res.iValue = lua_tointeger(handle, -1);
         }
         else if (lua_isnumber(handle, -1))
         {
            res.type = tDouble;
            res.dValue = lua_tonumber(handle, -1);
         }
         else if (lua_isstring(handle, -1))
         {
            res.type = tString;
            res.sValue = lua_tostring(handle, -1);
         }
         else
         {
            res.type = tNil;
            res.isNil = true;
            tell(eloAlways, "Warning: Lua expression returned no value: '%s'", expression);
         }

         lua_pop(handle, 1);                      // pop return value from stack
         lua_pop(handle, lua_gettop(handle));     // pop function from stack
      }
      else
      {
         tell(eloAlways, "Error: Lua execute '%s': %s", expression, lua_tostring(handle, -1));
         lua_pop(handle, lua_gettop(handle));
         return fail;
      }
   }
   else
   {
      free(code);

      tell(eloAlways, "Error: '%s'", lua_tostring(handle, lua_gettop(handle)));
      lua_pop(handle, lua_gettop(handle));
      return fail;
   }

   return success;
}

//**************************************************************************
// LuaSol – Init / Exit
//**************************************************************************

int LuaSol::init()
{
   luaL_openlibs(lua.lua_state());
   return success;
}

int LuaSol::exit()
{
   return success;   // sol::state cleans up via RAII
}

//**************************************************************************
// LuaSol – Configure (type registration, bindings)
//**************************************************************************

int LuaSol::configure(std::function<void(sol::state&)> fn)
{
   fn(lua);
   return success;
}

//**************************************************************************
// LuaSol – push globals via callback
//**************************************************************************

int LuaSol::push(std::function<void(sol::state&)> fn)
{
   fn(lua);
   return done;
}

//**************************************************************************
// LuaSol – Load Script
//**************************************************************************

int LuaSol::load(const char* script)
{
   auto result = lua.safe_script(script, sol::script_pass_on_error);

   if (!result.valid())
   {
      sol::error err = result;
      tell(eloAlways, "Error: Lua load: '%s'", err.what());
      return fail;
   }

   return success;
}

//**************************************************************************
// LuaSol – Syntax Check
//**************************************************************************

int LuaSol::syntaxCheck(const char* script, std::string& error)
{
   char* code {};
   asprintf(&code, "function execute()\n%s\nend", script);

   lua_State* L = lua.lua_state();
   int rc = luaL_loadstring(L, code);
   free(code);

   if (rc != LUA_OK)
   {
      error = lua_tostring(L, -1);
      lua_pop(L, 1);
      return fail;
   }

   lua_pop(L, 1);
   return success;
}

//**************************************************************************
// LuaSol – fillResult (private helper)
//**************************************************************************

void LuaSol::fillResult(sol::object obj, Result& res)
{
   if (obj.get_type() == sol::type::boolean)
   {
      res.type = Lua::tBoolean;
      res.bValue = obj.as<bool>();
   }
   else if (obj.is<lua_Integer>())
   {
      res.type = Lua::tInteger;
      res.iValue = static_cast<int>(obj.as<lua_Integer>());
   }
   else if (obj.is<lua_Number>())
   {
      res.type = Lua::tDouble;
      res.dValue = static_cast<double>(obj.as<lua_Number>());
   }
   else if (obj.get_type() == sol::type::string)
   {
      res.type = Lua::tString;
      res.sValue = obj.as<std::string>();
   }
   else
   {
      res.type = Lua::tNil;
      res.isNil = true;
   }
}

//**************************************************************************
// LuaSol – Execute Function
//**************************************************************************

int LuaSol::executeFunction(const char* function, const std::vector<std::string>& arguments, Result& res)
{
   res = {};

   sol::object fnObj = lua[function];

   if (!fnObj.valid() || fnObj.get_type() != sol::type::function)
   {
      tell(eloAlways, "Error: Function '%s' not found", function);
      return fail;
   }

   for (const auto& arg : arguments)
      tell(eloLua, "LUA: Add parameter '%s'", arg.c_str());

   sol::protected_function fn = fnObj;
   auto pfr = fn(sol::as_args(arguments));

   if (!pfr.valid())
   {
      sol::error err = pfr;
      tell(eloAlways, "Error: '%s'\n", err.what());
      return fail;
   }

   fillResult(pfr, res);
   return success;
}

//**************************************************************************
// LuaSol – Execute Expression
//**************************************************************************

int LuaSol::executeExpression(const char* expression, const std::vector<std::string>& arguments, Result& res)
{
   res = {};

   char* code {};
   asprintf(&code, "function execute()\n  %s\nend", expression);

   tell(eloLua, "LUA: Calling '%.20s ..'", code);

   auto loadResult = lua.safe_script(code, sol::script_pass_on_error);
   free(code);

   if (!loadResult.valid())
   {
      sol::error err = loadResult;
      tell(eloAlways, "Error: Lua load execute: '%s'", err.what());
      return fail;
   }

   sol::protected_function fn = lua["execute"];

   if (!fn.valid())
   {
      tell(eloAlways, "Error: 'execute' function not defined");
      return fail;
   }

   auto pfr = fn(sol::as_args(arguments));

   if (!pfr.valid())
   {
      sol::error err = pfr;
      tell(eloAlways, "Error: Lua execute '%s': %s", expression, err.what());
      return fail;
   }

   sol::object obj = pfr;

   if (obj.get_type() == sol::type::nil || obj.get_type() == sol::type::none)
      tell(eloAlways, "Warning: Lua expression returned no value: '%s'", expression);

   fillResult(obj, res);
   return success;
}
