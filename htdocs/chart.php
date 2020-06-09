<?php

include("header.php");

include("pChart/class/pData.class.php");
include("pChart/class/pDraw.class.php");
include("pChart/class/pImage.class.php");

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
    printHeader();

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
