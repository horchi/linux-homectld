<?php

include("header.php");

include("pChart/class/pData.class.php");
include("pChart/class/pDraw.class.php");
include("pChart/class/pImage.class.php");

{
    printHeader();

    // -------------------------
    // establish db connection

    $mysqli = new mysqli($mysqlhost, $mysqluser, $mysqlpass, $mysqldb, $mysqlport);
    $mysqli->query("set names 'utf8'");
    $mysqli->query("SET lc_time_names = 'de_DE'");

    // ----------------
    // init

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
        . "&condition=" . $_SESSION['chart1'] . "&chartXLines=" . $_SESSION['chartXLines']
        . "&chartDiv=" . $_SESSION['chartDiv'] . "'/>\n";
    echo "  </div>\n";

    echo "  <div class=\"rounded-border chart\">\n";
    echo "    <img src='detail.php?from=" . $from . "&range=" . $range
        . "&condition=" . $_SESSION['chart2'] . "&chartXLines=" . $_SESSION['chartXLines']
        . "&chartDiv=" . $_SESSION['chartDiv'] . "'/>\n";
    echo "  </div>\n";

    include("footer.php");
}
?>
