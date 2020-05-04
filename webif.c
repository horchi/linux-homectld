//***************************************************************************
// poold / Linux - Heizungs Manager
// File webif.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 18.04.2020  Jörg Wendel
//***************************************************************************

#include <inttypes.h>

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
      uint64_t start = cTimeMs::Now();
      int addr = tableJobs->getIntValue("ADDRESS");
      const char* command = tableJobs->getStrValue("COMMAND");
      const char* data = tableJobs->getStrValue("DATA");
      int jobId = tableJobs->getIntValue("ID");

      tableJobs->find();
      tableJobs->setValue("DONEAT", time(0));
      tableJobs->setValue("STATE", "D");

      tell(eloDebug, "Processing WEBIF job %d '%s:0x%04x/%s'",
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

         createMd5("pool", defaultPwd);
         getConfigItem("user", webUser, "pool");
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
      }

      else if (strcasecmp(command, "apply-config") == 0)
      {
         readConfiguration();
         applyConfigurationSpecials();
         process();
         tableJobs->setValue("RESULT", "success:apply-config");
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

      else if (strcasecmp(command, "toggle-output-mode") == 0)
      {
         toggleOutputMode(addr, strcmp(data, "manual") == 0 ? omManual : omAuto);
         tableJobs->setValue("RESULT", "success:toggle-output-mode");
      }

      else if (strcasecmp(command, "setio") == 0)
      {
         int v = atoi(data);
         gpioWrite(addr, v);
         tableJobs->setValue("RESULT", "success:setio");
      }

      else if (strcasecmp(command, "toggleio") == 0)
      {
         toggleIo(addr);
         tableJobs->setValue("RESULT", "success:toggled");
      }

      else if (strcasecmp(command, "toggleio2") == 0)
      {
         toggleIo(addr);
         usleep(300000);
         toggleIo(addr);
         tableJobs->setValue("RESULT", "success:toggled2");
      }

      else if (strcasecmp(command, "getio") == 0)
      {
         char* buf {nullptr};

         // tell(0, "DEBUG: reading digitalOutputStates '%d'", addr);
         // for (auto it = digitalOutputStates.begin(); it != digitalOutputStates.end(); ++it)
         // tell(0, "DEBUG: %d -> %d/%d/%d", it->first, it->second.state, it->second.mode, it->second.opt);

         bool v = digitalOutputStates[addr].state;
         OutputMode m = digitalOutputStates[addr].mode;
         int o = digitalOutputStates[addr].opt;
         asprintf(&buf, "success:%d#%d:%s:%d", addr, v, m == omManual ? "manual" : "auto", o);
         tableJobs->setValue("RESULT", buf);
         free(buf);
      }

      else if (strcasecmp(command, "daemon-state") == 0)
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
      else if (strcasecmp(command, "syslog") == 0)
      {
         FILE* fp;
         char line[200+TB] {'\0'};
         const char* name = "/var/log/syslog";

         if ((fp = fopen(name, "r")))
         {
            std::string result;
            int count {0};
            int n {0};
            char* buf;

            while (fgets(line, 200, fp))
            {
               int len = strlen(line);

               if (len && line[len-1] == '\n')
                  count++;
            }

            fseek(fp, 0, SEEK_SET);

            while (fgets(line, 200, fp))
            {
               int len = strlen(line);

               if (len && line[len-1] == '\n')
               {
                  if (n++ > count - 100)
                     result += line + std::string("<br/>");
               }
            }

            fclose(fp);

            asprintf(&buf, "success:%s", result.c_str());
            tableJobs->setValue("RESULT", buf);
            free(buf);
         }
         else
         {
            tell(eloAlways, "Error: Failed to open '%s', %s", name, strerror(errno));
            tableJobs->setValue("RESULT", "fail:syslog");
         }
      }

      else
      {
         tell(eloAlways, "Warning: Ignoring unknown WEBIF job '%s'", command);
         tableJobs->setValue("RESULT", "fail:unknown command");
      }

      tableJobs->store();

      tell(eloDebug, "Processing WEBIF job %d done with '%s' after %" PRIu64 " ms",
           jobId, tableJobs->getStrValue("RESULT"), cTimeMs::Now() - start);
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
