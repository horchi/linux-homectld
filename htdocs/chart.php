<?php

session_start();

include("config.php");
include("pChart/class/pData.class.php");
include("pChart/class/pDraw.class.php");
include("pChart/class/pImage.class.php");

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

// ---------------------------------------------------------------------------
// Read Config Item
// ---------------------------------------------------------------------------

function mysqli_result($res, $row=0, $col=0)
{
   $numrows = mysqli_num_rows($res);

   if ($numrows && $row <= ($numrows-1) && $row >=0)
   {
      mysqli_data_seek($res,$row);
      $resrow = (is_numeric($col)) ? mysqli_fetch_row($res) : mysqli_fetch_assoc($res);

       if (isset($resrow[$col]))
         return $resrow[$col];
   }
   return false;
}

function readConfigItem($name, &$value, $default = "")
{
   global $mysqli;

   // new version - read direct from config table

   $result = $mysqli->query("select value from config where owner = 'poold' and name = '$name'")
         or die("Error" . $mysqli->error);

   if ($result->num_rows > 0)
     $value = mysqli_result($result, 0, "value");
   else
     $value = $default;

   return 0;
}

readConfigItem("chartStart", $_SESSION['chartStart'], "7");
readConfigItem("chartDiv", $_SESSION['chartDiv'], "25");
readConfigItem("chart1", $_SESSION['chart1'], "0,1,113,18");
readConfigItem("chart2", $_SESSION['chart2'], "118,225,21,25,4,8");
readConfigItem("localLoginNetmask", $_SESSION['localLoginNetmask']);

// ---------------------------------------------------------------------------
// Date Picker
// ---------------------------------------------------------------------------

function datePicker($title, $name, $year, $day, $month)
{
   $html = "";
   $startyear = date("Y")-10;
   $endyear = date("Y")+1;

   $months = array('','Januar','Februar','März','April','Mai',
                   'Juni','Juli','August', 'September','Oktober','November','Dezember');

   if ($title != "")
      $html = $title . ": ";

   // day

   $html .= "  <select name=\"" . $name . "day\">\n";

   for ($i = 1; $i <= 31; $i++)
   {
      $sel = $i == $day  ? "SELECTED" : "";
      $html .= "     <option value='$i' " . $sel . ">$i</option>\n";
   }

   $html .= "  </select>\n";

   // month

   $html .= "  <select name=\"" . $name . "month\">\n";

   for ($i = 1; $i <= 12; $i++)
   {
      $sel = $i == $month ? "SELECTED" : "";
      $html .= "     <option value='$i' " . $sel . ">$months[$i]</option>\n";
   }

   $html.="  </select>\n";

   // year

   $html .= "  <select name=\"" . $name . "year\">\n";

   for ($i = $startyear; $i <= $endyear; $i++)
   {
      $sel = $i == $year  ? "SELECTED" : "";
      $html .= "     <option value='$i' " . $sel . ">$i</option>\n";
   }

   $html .= "  </select>\n";

   return $html;
}

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


    // -------------------------
    // establish db connection

    $mysqli = new mysqli($mysqlhost, $mysqluser, $mysqlpass, $mysqldb, $mysqlport);
    $mysqli->query("set names 'utf8'");
    $mysqli->query("SET lc_time_names = 'de_DE'");

    // ----------------
    // init

    define("tmeSecondsPerDay", 86400);

    $days = $_SESSION['chartStart'];

    $day   = isset($_GET['csday'])   ? $_GET['csday']   : (int)date("d", time() - tmeSecondsPerDay * $days);
    $month = isset($_GET['csmonth']) ? $_GET['csmonth'] : (int)date("m", time() - tmeSecondsPerDay * $days);
    $year  = isset($_GET['csyear'])  ? $_GET['csyear']  : (int)date("Y", time() - tmeSecondsPerDay * $days);
    $hour  = date("H", time());
    $min   = date("i", time());
    $sec   = date("s", time());

    $range = isset($_GET['crange'])  ? $_GET['crange']  : $_SESSION['chartStart'];
    $range = ($range > 7) ? 31 : (($range > 1) ? 7 : 1);

    $from = date_create_from_format('!Y-m-d H:i:s', $year.'-'.$month.'-'.$day." ".$hour.":".$min.":".$sec)->getTimestamp();

    if ($from > (time() - 60))
        $from = date_create_from_format('!Y-m-d H:i:s', $year.'-'.$month.'-'.$day." 00:00:00")->getTimestamp();

    echo "  <div class=\"rounded-border\" id=\"aSelect\">\n";
    echo "    <form name='navigation' method='get'>\n";
    echo "      Zeitraum der Charts<br/>\n";
    echo datePicker("", "cs", $year, $day, $month);

    echo "      <select name=\"crange\">\n";
    echo "        <option value='1' "  . ($range == 1  ? "SELECTED" : "") . ">Tag</option>\n";
    echo "        <option value='7' "  . ($range == 7  ? "SELECTED" : "") . ">Woche</option>\n";
    echo "        <option value='31' " . ($range == 31 ? "SELECTED" : "") . ">Monat</option>\n";
    echo "      </select>\n";
    echo "      <input type=submit value=\"Go\">\n";
    echo "    </form>\n";
    echo "  </div>\n";

    echo "  <div id=\"imgDiv\" class=\"rounded-border chart\">\n";
    echo "    <img src='detail.php?from=" . $from . "&range=" . $range
        . "&condition=" . $_SESSION['chart1'] . "&chartXLines=1"
        . "&chartDiv=" . $_SESSION['chartDiv'] . "'/>\n";
    echo "  </div>\n";

    echo "  <div class=\"rounded-border chart\">\n";
    echo "    <img src='detail.php?from=" . $from . "&range=" . $range
        . "&condition=" . $_SESSION['chart2'] . "&chartXLines=1"
        . "&chartDiv=" . $_SESSION['chartDiv'] . "'/>\n";
    echo "  </div>\n";

    echo "    </div>\n  </body>\n</html>";
}
?>
