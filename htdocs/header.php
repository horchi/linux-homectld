<?php

session_start();

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

readConfigItem("chartStart", $_SESSION['chartStart'], "7");
readConfigItem("chartDiv", $_SESSION['chartDiv'], "25");
readConfigItem("chart1", $_SESSION['chart1'], "0,1,113,18");
readConfigItem("chart2", $_SESSION['chart2'], "118,225,21,25,4,8");
readConfigItem("localLoginNetmask", $_SESSION['localLoginNetmask']);

function printHeader()
{
   $img = "img/logo.png";

   // ----------------
   // HTML Head

   echo "<!DOCTYPE html>\n";
   echo "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"de\" lang=\"de\">\n";
   echo "  <head>\n";
   echo "    <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\"/>\n";

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
   echo "    <title>Pool Control</title>\n";
   echo "  </head>\n";
   echo "  <body>\n";

   // menu button bar ...

   echo "    <nav class=\"fixed-menu1\">\n";
   echo "      <a href=\"index.html\"><button class=\"rounded-border button1\">Dashboard</button></a>\n";
   echo "      <a href=\"list.html\"><button class=\"rounded-border button1\">Liste</button></a>\n";
   echo "      <a href=\"chart.php\"><button class=\"rounded-border button1\">Charts</button></a>\n";
   echo "      <a href=\"maincfg.html\"><button class=\"rounded-border button1\">Setup</button></a>\n";
   echo "    </nav>\n";

   echo "    <div class=\"content\">\n";
}
?>
