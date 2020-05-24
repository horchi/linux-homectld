<?php

// -------------------------
// chaeck login

if (haveLogin())
{
   echo "      <div class=\"fixed-menu2\">\n";
   echo "        <a href=\"basecfg.php\"><button class=\"rounded-border button2\">Allg. Konfiguration</button></a>\n";
   echo "        <a href=\"settings.php\"><button class=\"rounded-border button2\">IO Setup</button></a>\n";
   echo "        <a href=\"syslog.php\"><button class=\"rounded-border button2\">Syslog</button></a>\n";
   echo "      </div>\n";
}

?>
