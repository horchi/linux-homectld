<?php

session_start();

$mysqlport = 3306;

$p4WebVersion = "<VERSION>";

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

     readConfigItem("interval", $_SESSION['interval']);

     readConfigItem("mail", $_SESSION['mail']);
     readConfigItem("stateMailTo", $_SESSION['stateMailTo']);
     readConfigItem("errorMailTo", $_SESSION['errorMailTo']);
     readConfigItem("mailScript", $_SESSION['mailScript']);

     readConfigItem("webUrl",  $_SESSION['webUrl'], "http://127.0.0.1/p4");
     readConfigItem("haUrl", $_SESSION['haUrl']);

     readConfigItem("filterPumpTimes", $_SESSION['filterPumpTimes']);
     readConfigItem("uvcLightTimes", $_SESSION['uvcLightTimes']);

     readConfigItem("tPoolMax", $_SESSION['tPoolMax']);
     readConfigItem("tSolarDelta", $_SESSION['tSolarDelta']);

     readConfigItem("w1AddrPool", $_SESSION['w1AddrPool']);
     readConfigItem("w1AddrSolar", $_SESSION['w1AddrSolar']);
     readConfigItem("w1AddrSuctionTube", $_SESSION['w1AddrSuctionTube']);
     readConfigItem("w1AddrAir", $_SESSION['w1AddrAir']);
     readConfigItem("poolLightColorToggle", $_SESSION['poolLightColorToggle'], "0");
     readConfigItem("invertDO", $_SESSION['invertDO'], "0");

     readConfigItem("aggregateHistory", $_SESSION['aggregateHistory'], "365");
     readConfigItem("aggregateInterval", $_SESSION['aggregateInterval'], "15");

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
   echo "    <link rel=\"shortcut icon\" href=\"$img\" type=\"image/png\"/>\n";
   echo "    <link rel=\"icon\" href=\"$img\" type=\"image/png\"/>\n";
   echo "    <link rel=\"apple-touch-icon-precomposed\" sizes=\"144x144\" href=\"$img\" type=\"image/png\"/>\n";
   echo "    <link rel=\"stylesheet\" type=\"text/css\" href=\"stylesheet.css\"/>\n";
   echo "    <script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.10.2/jquery.min.js\"></script>\n";
   echo "    <script src=\"https://cdnjs.cloudflare.com/ajax/libs/snap.svg/0.4.1/snap.svg-min.js\"></script>\n";
   echo "    <script type=\"text/JavaScript\" src=\"jfunctions.js\"></script>\n";
   echo "    <title>Pool Control</title>\n";
   echo "  </head>\n";
   echo "  <body>\n";

   // menu button bar ...

   echo "    <nav class=\"fixed-menu1\">\n";
   echo "      <a href=\"main.php\"><button class=\"rounded-border button1\">Aktuell</button></a>\n";
   echo "      <a href=\"dashboard.php\"><button class=\"rounded-border button1\">Dashboard</button></a>\n";
   echo "      <a href=\"chart.php\"><button class=\"rounded-border button1\">Charts</button></a>\n";

   if (haveLogin())
   {
      echo "      <a href=\"basecfg.php\"><button class=\"rounded-border button1\">Setup</button></a>\n";
      echo "      <a href=\"logout.php\"><button class=\"rounded-border button1\">Logout</button></a>\n";
   }
   else
   {
      echo "      <a href=\"login.php\"><button class=\"rounded-border button1\">Login</button></a>\n";
   }

   if (isset($_SESSION['haUrl']) && $_SESSION['haUrl'] != "")
      echo "      <a href=\"" . $_SESSION['haUrl'] . "\"><button class=\"rounded-border button1\">HADashboard</button></a>\n";

   echo "    </nav>\n";
   /* echo "    <div class=\"menu\">\n"; */
   /* echo "    </div>\n"; */
   echo "    <div class=\"content\">\n";
?>
<script>

function pageShow()
{
   'use strict';

   var
      content = 0,
      nav1 = 0,
      nav2 = 0,
      nav3 = 0;

   content = document.querySelector('.content');
   nav1 = document.querySelector('.fixed-menu1');
   nav2 = document.querySelector('.fixed-menu2');
   nav3 = document.querySelector('.menu');

   if (!content)
      return true;

   if (nav3)
   {
      content.style.top = nav1.clientHeight + nav2.clientHeight + nav3.clientHeight + 'px';
      nav1.style.top = 0 + 'px';
      nav2.style.top = nav1.clientHeight + 'px';
      nav3.style.top = nav1.clientHeight + nav1.clientHeight + 'px';
   }
   else if (nav2)
   {
      content.style.top = nav1.clientHeight + nav2.clientHeight + 'px';
      nav1.style.top = 0 + 'px';
      nav2.style.top = nav1.clientHeight + 'px';
   }
   else if (nav1)
   {
      content.style.top = nav1.clientHeight + 'px';
      nav1.style.top = 0 + 'px';
   }
}

(function()
{
   'use strict';

   var
      navHeight = 0,
      navTop = 0,
      scrollCurr = 0,
      scrollPrev = 0,
      scrollDiff = 0,
      content = 0,
      nav1 = 0,
      nav2 = 0,
      nav3 = 0;

   window.addEventListener('scroll', function()
   {
      nav1 = document.querySelector('.fixed-menu1');
      content = document.querySelector('.content');

      if (!nav1 || !content)
         return true;

      pageShow();

      navHeight = nav1.clientHeight;
      scrollCurr = window.pageYOffset;
      scrollDiff = scrollPrev - scrollCurr;
      navTop = parseInt(window.getComputedStyle(content).getPropertyValue('top')) - scrollCurr;

      if (scrollCurr <= 0)              // Scroll to top: fix navbar to top
         nav1.style.top = '0px';
      else
         nav1.style.top = navTop - nav1.clientHeight + 'px';

      /* else if (scrollDiff > 0)          // Scroll up: show navbar */
      /*    nav1.style.top = (navTop > 0 ? 0 : navTop) + 'px'; */
      /* else if (scrollDiff < 0)          // Scroll down: hide navbar */
      /*    nav1.style.top = (Math.abs(navTop) > navHeight ? -navHeight : navTop) + 'px'; */

      nav2 = document.querySelector('.fixed-menu2');

      if (nav2)
      {
         navHeight = nav2.clientHeight;
         navTop = parseInt(window.getComputedStyle(nav2).getPropertyValue('top')) + scrollDiff;

         if (scrollCurr <= 0)           // Scroll to top: fix navbar to top
            nav2.style.top = nav1.clientHeight + 'px';
         else if (scrollDiff > 0)       // Scroll up: show navbar
            nav2.style.top = (navTop > 0 ? nav1.clientHeight : navTop) + 'px';
         else if (scrollDiff < 0)       // Scroll down: hide navbar
         {
            if (scrollCurr <= nav1.clientHeight)
               nav2.style.top = '0px';
            else
               nav2.style.top = (Math.abs(navTop) > navHeight ? -navHeight : navTop) + 'px';
         }
      }

      nav3 = document.querySelector('.menu');

      if (nav3)
      {
         navHeight = nav3.clientHeight;
         navTop = parseInt(window.getComputedStyle(nav3).getPropertyValue('top')) + scrollDiff;

         if (scrollCurr <= 0)           // Scroll to top: fix navbar to top
            nav3.style.top = '76px';
         else if (scrollDiff > 0)       // Scroll up: show navbar
            nav3.style.top = (navTop > 0 ? 76 : navTop) + 'px';
         else if (scrollDiff < 0)       // Scroll down: hide navbar
         {
            if (scrollCurr <= nav1.clientHeight)
               nav3.style.top = '0px';
            else
               nav3.style.top = (Math.abs(navTop) > navHeight ? -navHeight : navTop) + 'px';
         }
      }

      scrollPrev = scrollCurr;          // remember last scroll position
   });

}());

(function()
{
   'use strict';

   if (window.addEventListener)
      window.addEventListener("load", pageShow, false);
   else if (window.attachEvent)
   {
      window.attachEvent("onload", pageShow);
      window.attachEvent('pageshow', pageShow);
   }
   else
      window.onload = pageShow;

}());

</script>
<?php
}

?>
