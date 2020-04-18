//***************************************************************************
// p4d / Linux - Heizungs Manager
// File webif.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 18.04.2020  Jörg Wendel
//***************************************************************************

#include "lib/common.h"

#include "poold.h"

//***************************************************************************
// Perform WEBIF Requests
//***************************************************************************

int Poold::performWebifRequests()
{
   tableJobs->clear();

   for (int f = selectPendingJobs->find(); f; f = selectPendingJobs->fetch())
   {
      int start = time(0);
      int addr = tableJobs->getIntValue("ADDRESS");
      const char* command = tableJobs->getStrValue("COMMAND");
      const char* data = tableJobs->getStrValue("DATA");
      int jobId = tableJobs->getIntValue("ID");

      tableJobs->find();
      tableJobs->setValue("DONEAT", time(0));
      tableJobs->setValue("STATE", "D");

      tell(eloAlways, "Processing WEBIF job %d '%s:0x%04x/%s'",
           jobId, command, addr, data);

      if (strcasecmp(command, "test-mail") == 0)
      {
         char* subject = strdup(data);
         char* body = 0;

         if ((body = strchr(subject, ':')))
         {
            *body = 0; body++;

            tell(eloDetail, "Test mail requested with: '%s/%s'", subject, body);

            if (isEmpty(mailScript))
               tableJobs->setValue("RESULT", "fail:missing mailscript");
            else if (!fileExists(mailScript))
               tableJobs->setValue("RESULT", "fail:mail-script not found");
            else if (isEmpty(stateMailTo))
               tableJobs->setValue("RESULT", "fail:missing-receiver");
            else if (sendMail(stateMailTo, subject, body, "text/plain") != success)
               tableJobs->setValue("RESULT", "fail:send failed");
            else
               tableJobs->setValue("RESULT", "success:mail sended");
         }
      }

      else if (strcasecmp(command, "check-login") == 0)
      {
         char* webUser = 0;
         char* webPass = 0;
         md5Buf defaultPwd;

         createMd5("p4-3200", defaultPwd);

         getConfigItem("user", webUser, "p4");
         getConfigItem("passwd", webPass, defaultPwd);

         char* user = strdup(data);
         char* pwd = 0;

         if ((pwd = strchr(user, ':')))
         {
            *pwd = 0; pwd++;

            tell(eloDetail, "%s/%s", pwd, webPass);

            if (strcmp(webUser, user) == 0 && strcmp(pwd, webPass) == 0)
               tableJobs->setValue("RESULT", "success:login-confirmed");
            else
               tableJobs->setValue("RESULT", "fail:login-denied");
         }

         free(webPass);
         free(webUser);
         free(user);
      }

/*      else if (strcasecmp(command, "call-script") == 0)
      {
         const char* result;

         if (callScript(data, result) != success)
         {
            char* responce;
            asprintf(&responce, "fail:%s", result);
            tableJobs->setValue("RESULT", responce);
            free(responce);
         }
         else
         {
            tableJobs->setValue("RESULT", "success:done");
         }
         }*/

      else if (strcasecmp(command, "write-config") == 0)
      {
         char* name = strdup(data);
         char* value = 0;

         if ((value = strchr(name, ':')))
         {
            *value = 0; value++;

            setConfigItem(name, value);

            tableJobs->setValue("RESULT", "success:stored");
         }

         free(name);

         // read the config from table to apply changes

         readConfiguration();
      }

      else if (strcasecmp(command, "read-config") == 0)
      {
         char* name = strdup(data);
         char* buf = 0;
         char* value = 0;
         char* def = 0;

         if ((def = strchr(name, ':')))
         {
            *def = 0;
            def++;
         }

         getConfigItem(name, value, def ? def : "");

         asprintf(&buf, "success:%s", value);
         tableJobs->setValue("RESULT", buf);

         free(name);
         free(buf);
         free(value);
      }

/*      else if (strcasecmp(command, "getv") == 0)
      {
         Value v(addr);

         tableValueFacts->clear();
         tableValueFacts->setValue("TYPE", "VA");
         tableValueFacts->setValue("ADDRESS", addr);

         if (tableValueFacts->find())
         {
            double factor = tableValueFacts->getIntValue("FACTOR");
            const char* unit = tableValueFacts->getStrValue("UNIT");

            if (request->getValue(&v) == success)
            {
               char* buf = 0;

               asprintf(&buf, "success:%.2f%s", v.value / factor, unit);
               tableJobs->setValue("RESULT", buf);
               free(buf);
            }
         }
         }*/

      else if (strcasecmp(command, "p4d-state") == 0)
      {
         struct tm tim = {0};

         double averages[3];
         char dt[10];
         char d[100];
         char* buf;

         memset(averages, 0, sizeof(averages));
         localtime_r(&nextAt, &tim);
         strftime(dt, 10, "%H:%M:%S", &tim);
         toElapsed(time(0)-startedAt, d);

         getloadavg(averages, 3);

         asprintf(&buf, "success:%s#%s#%s#%3.2f %3.2f %3.2f",
                  dt, VERSION, d, averages[0], averages[1], averages[2]);

         tableJobs->setValue("RESULT", buf);
         free(buf);
      }


      else
      {
         tell(eloAlways, "Warning: Ignoring unknown job '%s'", command);
         tableJobs->setValue("RESULT", "fail:unknown command");
      }

      tableJobs->store();

      tell(eloAlways, "Processing WEBIF job %d done with '%s' after %ld seconds",
           jobId, tableJobs->getStrValue("RESULT"),
           time(0) - start);
   }

   selectPendingJobs->freeResult();

   return success;
}

//***************************************************************************
// Cleanup WEBIF Requests
//***************************************************************************

int Poold::cleanupWebifRequests()
{
   int status;

   // delete jobs older than 2 days

   tell(eloAlways, "Cleanup jobs table with history of 2 days");

   tableJobs->clear();
   tableJobs->setValue("REQAT", time(0) - 2*tmeSecondsPerDay);
   status = cleanupJobs->execute();

   return status;
}

//***************************************************************************
// Call Script
//***************************************************************************
/*
int Poold::callScript(const char* scriptName, const char*& result)
{
   int status;
   const char* path;

   result = "";

   tableScripts->clear();
   tableScripts->setValue("NAME", scriptName);

   if (!selectScriptByName->find())
   {
      selectScriptByName->freeResult();
      tell(eloAlways, "Script '%s' not found in database", scriptName);
      result = "script name not found";
      return fail;
   }

   selectScriptByName->freeResult();
   path = tableScripts->getStrValue("PATH");

   if (!fileExists(path))
   {
      tell(eloAlways, "Path '%s' not found", path);
      result = "path not found";
      return fail;
   }

   if ((status = system(path)) == -1)
   {
      tell(eloAlways, "Called script '%s' failed", path);
      return fail;
   }

   tell(eloAlways, "Called script '%s' at path '%s', exit status was (%d)", scriptName, path, status);

   return success;
}
*/
