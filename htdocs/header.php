<?php

session_start();

$webVersion  = "<VERSION>";
$daemonTitle = "Pool Control Daemon";
$daemonName  = "poold";

include("config.php");
include("functions.php");

   // -------------------------
   // establish db connection

   $mysqli = new mysqli($mysqlhost, $mysqluser, $mysqlpass, $mysqldb, $mysqlport);

   if (mysqli_connect_error())
   {
      die('Connect Error (' . mysqli_connect_errno() . ') '
            . mysqli_connect_error() . ". Can't connect to " . $mysqlhost . ' at ' . $mysqlport);
   }

   $mysqli->query("set names 'utf8'");
   $mysqli->query("SET lc_time_names = 'de_DE'");

  // ----------------
  // Configuration

  // if (!isset($_SESSION['initialized']) || !$_SESSION['initialized'])
  {
     $_SESSION['initialized'] = true;

     // ------------------
     // get configuration

     readConfigItem("refreshWeb", $_SESSION['refreshWeb'], 60);

     readConfigItem("addrsMain", $_SESSION['addrsMain'], "");
     readConfigItem("addrsMainMobile", $_SESSION['addrsMainMobile'], "0,1,4,118,119,120");
     readConfigItem("addrsDashboard", $_SESSION['addrsDashboard'], "");

     readConfigItem("chartStart", $_SESSION['chartStart'], "7");
     readConfigItem("chartDiv", $_SESSION['chartDiv'], "25");
     readConfigItem("chartXLines", $_SESSION['chartXLines'], "0");
     readConfigItem("chart1", $_SESSION['chart1'], "0,1,113,18");
     readConfigItem("chart2", $_SESSION['chart2'], "118,225,21,25,4,8");

     readConfigItem("user", $_SESSION['user']);
     readConfigItem("passwd", $_SESSION['passwd']);
     readConfigItem("localLoginNetmask", $_SESSION['localLoginNetmask']);

     readConfigItem("interval", $_SESSION['interval']);

     readConfigItem("mail", $_SESSION['mail']);
     readConfigItem("stateMailTo", $_SESSION['stateMailTo']);
     readConfigItem("errorMailTo", $_SESSION['errorMailTo']);
     readConfigItem("mailScript", $_SESSION['mailScript']);

     readConfigItem("webUrl",  $_SESSION['webUrl'], "http://127.0.0.1/pool");
     readConfigItem("haUrl", $_SESSION['haUrl']);

     readConfigItem("aggregateHistory", $_SESSION['aggregateHistory'], "365");
     readConfigItem("aggregateInterval", $_SESSION['aggregateInterval'], "15");

     readConfigItem("filterPumpTimes", $_SESSION['filterPumpTimes']);
     readConfigItem("uvcLightTimes", $_SESSION['uvcLightTimes']);
     readConfigItem("poolLightTimes", $_SESSION['poolLightTimes']);

     readConfigItem("tPoolMax", $_SESSION['tPoolMax']);
     readConfigItem("tSolarDelta", $_SESSION['tSolarDelta']);
     readConfigItem("showerDuration", $_SESSION['showerDuration']);

     readConfigItem("w1AddrPool", $_SESSION['w1AddrPool']);
     readConfigItem("w1AddrSolar", $_SESSION['w1AddrSolar']);
     readConfigItem("w1AddrSuctionTube", $_SESSION['w1AddrSuctionTube']);
     readConfigItem("w1AddrAir", $_SESSION['w1AddrAir']);
     readConfigItem("poolLightColorToggle", $_SESSION['poolLightColorToggle'], "0");
     readConfigItem("invertDO", $_SESSION['invertDO'], "0");

     readConfigItem("hassMqttUrl", $_SESSION['hassMqttUrl']);

     // ------------------
     // check for defaults

     if ($_SESSION['chartXLines'] == "")
        $_SESSION['chartXLines'] = "0";
     else
        $_SESSION['chartXLines'] = "1";

     // $mysqli->close();
  }

function printHeader($refresh = 0)
{
   $img = "img/logo.png";

   // ----------------
   // HTML Head

   echo "<!DOCTYPE html>\n";
   echo "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"de\" lang=\"de\">\n";
   echo "  <head>\n";
   echo "    <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\"/>\n";

   if ($refresh)
      echo "    <meta http-equiv=\"refresh\" content=\"$refresh\"/>\n";

   echo "    <meta name=\"author\" content=\"Jörg Wendel\"/>\n";
   echo "    <meta name=\"copyright\" content=\"Jörg Wendel\"/>\n";
   echo "    <meta name=\"viewport\" content=\"initial-scale=1.0, width=device-width, user-scalable=no, maximum-scale=1, minimum-scale=1\"/>\n";

   echo "    <meta name=\"mobile-web-app-capable\" content=\"yes\"/>\n";
   echo "    <meta name=\"apple-mobile-web-app-capable\" content=\"yes\"/>\n";
   echo "    <meta name=\"apple-mobile-web-app-status-bar-style\" content=\"black-translucent\"/>\n";

   echo "    <link rel=\"shortcut icon\" href=\"$img\" type=\"image/png\"/>\n";
   echo "    <link rel=\"icon\" href=\"$img\" type=\"image/png\"/>\n";
   echo "    <link rel=\"apple-touch-icon-precomposed\" sizes=\"144x144\" href=\"$img\" type=\"image/png\"/>\n";
   echo "    <link rel=\"stylesheet\" type=\"text/css\" href=\"stylesheet.css\"/>\n";

   echo "    <script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.10.2/jquery.min.js\"></script>\n";
   echo "    <script type=\"text/JavaScript\" src=\"jfunctions.js\"></script>\n";
   echo "    <title>Pool Control</title>\n";
   echo "  </head>\n";
   echo "  <body>\n";

   // menu button bar ...

   echo "    <nav class=\"fixed-menu1\">\n";
   echo "      <a href=\"dashboard.html\"><button class=\"rounded-border button1\">Dashboard</button></a>\n";
   echo "      <a href=\"main.php\"><button class=\"rounded-border button1\">Liste</button></a>\n";
   echo "      <a href=\"chart.php\"><button class=\"rounded-border button1\">Charts</button></a>\n";
   echo "      <a href=\"maincfg.html\"><button class=\"rounded-border button1\">Setup</button></a>\n";

   if (haveLogin())
      echo "      <a href=\"logout.php\"><button class=\"rounded-border button1\">Logout</button></a>\n";
   else
      echo "      <a href=\"login.php\"><button class=\"rounded-border button1\">Login</button></a>\n";

   if (isset($_SESSION['haUrl']) && $_SESSION['haUrl'] != "")
      echo "      <a href=\"" . $_SESSION['haUrl'] . "\"><button class=\"rounded-border button1\">HADashboard</button></a>\n";

   echo "    </nav>\n";
   echo "    <div class=\"content\">\n";
}
?>
