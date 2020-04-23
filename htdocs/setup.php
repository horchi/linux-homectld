<?php

// -------------------------
// chaeck login

if (haveLogin())
{
   echo "      <div class=\"fixed-menu2\">\n";
   echo "        <a href=\"basecfg.php\"><button class=\"rounded-border button2\">Allg. Konfiguration</button></a>\n";
   echo "        <a href=\"settings.php\"><button class=\"rounded-border button2\">Aufzeichnung</button></a>\n";
   echo "      </div>\n";
}

?>
