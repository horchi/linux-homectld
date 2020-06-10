
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
