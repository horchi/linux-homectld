//***************************************************************************
// Home Systemd Interface
// File systemdctl.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 24.02.2024 - Jörg Wendel
//***************************************************************************

#include "systemdctl.h"

//***************************************************************************
// Init / Exit
//***************************************************************************

int SysCtl::init()
{
   int res {0};

   if (bus)
      exit();

   if ((res = sd_bus_open_system(&bus)) < 0)
   {
      tell(eloAlways, "Failed to connect to system bus: %s", strerror(-res));
      sd_bus_unref(bus);
      return fail;
   }

   return success;
}

int SysCtl::exit()
{
   if (bus)
      sd_bus_unref(bus);

   bus = nullptr;

   return success;
}

//***************************************************************************
// Get Property
//***************************************************************************

int SysCtl::getProperty(const char* unit, const char* property, std::string& value)
{
    sd_bus_error error {SD_BUS_ERROR_NULL};
    char* msg {};

    int res = sd_bus_get_property_string(bus,
                                         "org.freedesktop.systemd1",
                                         unit,
                                         "org.freedesktop.systemd1.Unit",
                                         property,
                                         &error,
                                         &msg);

    if (res < 0)
    {
       tell(eloAlways, "Failed to get %s.property '%s', error was '%s'", unit, property, error.message);
       sd_bus_error_free(&error);
       return fail;
    }

    value = msg;
    free(msg);
    sd_bus_error_free(&error);

    return success;
}

//***************************************************************************
// List Units
//***************************************************************************

int SysCtl::unitList(Services& services)
{
   sd_bus_error error {SD_BUS_ERROR_NULL};
   sd_bus_message* message {};

   int res = sd_bus_call_method(bus,
                                "org.freedesktop.systemd1",         // service to contact
                                "/org/freedesktop/systemd1",        // object path
                                "org.freedesktop.systemd1.Manager", // interface name
                                "ListUnits",                        // method name
                                &error,                             // object to return error in
                                &message,                           // return message on success
                                nullptr);

   if (res < 0)
   {
      tell(eloAlways, "Failed to issue method call '%s'", error.message);
      sd_bus_error_free(&error);
      sd_bus_message_unref(message);
      return fail;
   }

   // parse response

   res = sd_bus_message_enter_container(message, 'a', "(ssssssouso)");

   if (res < 0)
   {
      tell(eloAlways, "Failed to issue method call 'sd_bus_message_enter_containers': '%s'", error.message);
      sd_bus_error_free(&error);
      sd_bus_message_unref(message);
      return fail;
   }

   const char* primary_name {};
   const char* human_name {};
   const char* load_state {};
   const char* active_state {};
   const char* sub_state {};
   const char* following {};
   const char** unit_object_path {};
   uint32_t queued_job_id {};
   const char* job_type {};
   const char** job_object_path {};

   while ((res = sd_bus_message_read(message, "(ssssssouso)", &primary_name, &human_name, &load_state, &active_state, &sub_state, &following, &unit_object_path, &queued_job_id, &job_type, &job_object_path)) > 0)
   {
      if (strstr(primary_name, "user@") || strstr(primary_name, "modprobe@") || strcmp(sub_state, "plugged") == 0 || strcmp(sub_state, "mounted") == 0 || strcmp(sub_state, "exited") == 0)
         continue;

      printf("%s - %s  \n", primary_name, sub_state);

      if (strstr(primary_name, ".service"))
         services[primary_name] = {primary_name, human_name, load_state, active_state, sub_state};
   }

   if (res < 0)
      tell(eloAlways, "Failed to issue method call 'sd_bus_message_read': '%s'", strerror(-res));
   else
      res = sd_bus_message_exit_container(message);

   sd_bus_error_free(&error);
   sd_bus_message_unref(message);

   return success;
}

//***************************************************************************
// Unit Action
//***************************************************************************

int SysCtl::unitAction(const char* command, const char* unit)
{
   sd_bus_error error {SD_BUS_ERROR_NULL};
   sd_bus_message* message {};
   const char* path {};

   // Issue the method call and store the respons message in m

   std::string cmd = std::string(command) + "Unit";

   int res = sd_bus_call_method(bus,
                            "org.freedesktop.systemd1",          // service to contact
                            "/org/freedesktop/systemd1",         // object path
                            "org.freedesktop.systemd1.Manager",  // interface name
                            cmd.c_str(),                         // method name
                            &error,                              // object to return error in
                            &message,                            // return message on success
                            "ss",                                // input signature
                            unit,                                // first argument
                            "replace");                          // second argument

   if (res < 0)
   {
      tell(eloAlways, "Failed to issue method call: '%s'", error.message);
      sd_bus_error_free(&error);
      sd_bus_message_unref(message);
      return fail;
   }

   // parse response

   if (sd_bus_message_read(message, "o", &path) < 0)
   {
      tell(eloAlways, "Failed to parse response message: '%s'", strerror(-res));
      sd_bus_error_free(&error);
      sd_bus_message_unref(message);
      return fail;
   }

   tell(eloAlways, "Performed '%s' for '%s' [%s]", command, unit, path);

   return success;
}

//***************************************************************************
// Load Unit
//***************************************************************************

int SysCtl::loadUnit(const char* unit, std::string& unitPath)
{
   sd_bus_error error {SD_BUS_ERROR_NULL};
   sd_bus_message* message {};

   int res = sd_bus_call_method(bus,
                            "org.freedesktop.systemd1",         // service to contact
                            "/org/freedesktop/systemd1",        // object path
                            "org.freedesktop.systemd1.Manager", // interface name
                            "LoadUnit",                         // method name
                            &error,                             // object to return error in
                            &message,                           // return message on success
                            "s",                                // input signature
                            unit,                               // first argument
                            "");                                // second argument

   if (res < 0)
   {
      tell(eloAlways, "Failed to issue method call: '%s'", error.message);
      sd_bus_error_free(&error);
      sd_bus_message_unref(message);
      return fail;
   }

   // parse response

   const char* path {};

   if (sd_bus_message_read(message, "o", &path) < 0)
   {
      tell(eloAlways, "Failed to parse response message: '%s'", strerror(-res));
      sd_bus_error_free(&error);
      sd_bus_message_unref(message);
      return fail;
   }

   unitPath = path;

   return success;
}
