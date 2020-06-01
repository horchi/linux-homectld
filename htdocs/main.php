<?php

//***************************************************************************
// WEB Interface of poold / Linux - Pool Control
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 17.04.2020 - Jörg Wendel
//***************************************************************************

include("header.php");

printHeader($_SESSION['refreshWeb']);

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

  // -------------------------
  // get last time stamp

  $result = $mysqli->query("select max(time) from samples")
     or die("Error" . $mysqli->error);

  $row = $result->fetch_assoc();
  $max = $row['max(time)'];

  $result = $mysqli->query("select max(time), DATE_FORMAT(max(time),'%d. %M %Y   %H:%i') as maxPretty from samples")
     or die("Error" . $mysqli->error);
  $row = $result->fetch_assoc();
  $max = $row['max(time)'];
  $maxPretty = $row['maxPretty'];

  // ----------------
  // init

  $days = $_SESSION['chartStart'];

  $sday   = isset($_GET['sday'])   ? $_GET['sday']    : (int)date("d", time() - tmeSecondsPerDay * $days);
  $smonth = isset($_GET['smonth']) ? $_GET['smonth']  : (int)date("m", time() - tmeSecondsPerDay * $days);
  $syear  = isset($_GET['syear'])  ? $_GET['syear']   : (int)date("Y", time() - tmeSecondsPerDay * $days);
  $srange = isset($_GET['srange'])  ? $_GET['srange'] : $_SESSION['chartStart'];
  $hour  = date("H", time());
  $min   = date("i", time());
  $sec   = date("s", time());

  $range = ($srange > 7) ? 31 : (($srange > 1) ? 7 : 1);
  $from = date_create_from_format('!Y-m-d H:i:s', $syear.'-'.$smonth.'-'.$sday." ".$hour.":".$min.":".$sec)->getTimestamp();

  // ------------------
  // Buttons

  $action = "";

  if (isset($_POST["action"]))
     $action = htmlspecialchars($_POST["action"]);

  if ($action == "clearpeaks")
  {
      $mysqli->query("truncate table peaks")
          or die("<br/>Error" . $mysqli->error);

      echo "      <br/><div class=\"info\"><b><center>Peaks cleared</center></b></div><br/>\n";
  }

  echo "      <div class=\"stateInfo\">\n";
  printDaemonState();
  echo "      </div>\n";

  // ------------------
  // Sensor List
  {
     buildIdList(!isMobile() ? $_SESSION['addrsMain'] : $_SESSION['addrsMainMobile'], $sensors, $addrWhere, $ids);

      // select s.address as s_address, s.type as s_type, s.time as s_time, f.title, f.usrtitle from samples s left join peaks p on p.address = s.address and p.type = s.type join valuefacts f on f.address = s.address and f.type = s.type where f.state = 'A' and s.time = '2020-03-27 12:19:00'

     $strQueryBase = sprintf("select p.minv as p_min, p.maxv as p_max, s.address as s_address, s.type as s_type,
                             s.time as s_time, s.value as s_value, s.text as s_text,
                             f.title as f_title, f.usrtitle as f_usrtitle, f.unit as f_unit
                             from samples s left join peaks p on p.address = s.address and p.type = s.type
                             join valuefacts f on f.address = s.address and f.type = s.type");

     if ($addrWhere == "")
         $strQuery = sprintf("%s where s.time = '%s'", $strQueryBase, $max);
     else
         $strQuery = sprintf("%s where (%s) and s.time = '%s'", $strQueryBase, $addrWhere, $max);

     // tell("selecting " . " '" . $strQuery . "'");

     $result = $mysqli->query($strQuery)
         or die("Error" . $mysqli->error);

     echo "      <div class=\"rounded-border table2Col\">\n";
     echo "        <center>Messwerte vom $maxPretty</center>\n";

     $map = new \Ds\Map();
     $i = 0;

     while ($row = $result->fetch_assoc())
     {
         $id = "0x".dechex($row['s_address']).":".$row['s_type'];
         $map->put($id, $row);

         if ($addrWhere == "")
             $ids[$i++] = $id;
     }

     // get actual state of all 'DO'

     $sensors = new \Ds\Map();
     $state = requestAction("getallio", 3, 0, "", $response);

     if ($state == 0)
     {
        $arr = explode("#", $response);

        foreach ($arr as &$item)
        {
           if ($item != "")
           {
              list($pin, $data) = explode(":", $item, 2);
              $sensors->put($pin, $data);
           }
        }
     }

     // process ...

     for ($i = 0; $i < count($ids); $i++)
     {
         try
         {
             $row = $map[$ids[$i]];
         }
         catch (Exception $e)
         {
             echo "ID '".$ids[$i]."' not found, ignoring!";
         }

         $value = $row['s_value'];
         $text = $row['s_text'];
         $title = $row['f_usrtitle'] != "" ? $row['f_usrtitle'] : $row['f_title'];
         $unit = prettyUnit($row['f_unit']);
         $address = $row['s_address'];
         $type = $row['s_type'];
         $peak = "";
         $min = $row['p_min'];
         $max = $row['p_max'];
         $txtaddr = sprintf("0x%x", $address);
         $mode = 'auto';

         // case of DO request the actual state instead of the last stored value

         if ($type == 'DO')
         {
            if ($sensors->hasKey($address))
            {
               $data = $sensors[$address];
               list($value, $mode, $opt) = explode(":", $data, 3);
            }
         }

         if ($type != 'DI' && $type != 'DO' && $unit != '' && $unit != 'h' && $unit != 'Stunden')
             $peak = sprintf("(%s/%s)", $min, $max);

         if ($type == 'DI' || $type == 'DO')
             $value = $value == "1.00" ? "an" : "aus";
         else if ($row['f_unit'] == 'h' || $row['f_unit'] == 'U')
             $value = round($value, 0);

         if ($row['f_unit'] == 'T')
             $value = str_replace($wd_value, $wd_disp, $text);
         else if ($row['f_unit'] == 'zst')
             $value = $text;

         $url = "class=\"tableButton\" href=\"#\" onclick=\"window.open('detail.php?width=1200&height=600&address=$address&type=$type&from="
             . $from . "&range=" . $srange . "&chartXLines=" . $_SESSION['chartXLines'] . "&chartDiv="
             . $_SESSION['chartDiv'] . " ','_blank',"
             . "'scrollbars=yes,width=1200,height=600,resizable=yes,left=120,top=120')\"";

         echo "         <div class=\"mainrow\">\n";
         echo "           <span><a $url>$title</a></span>\n";
         echo "           <span>$value&nbsp;$unit &nbsp; <p style = \"display:inline;font-size:12px;font-style:italic;\">$peak</p></span>\n";
         echo "         </div>\n";
     }

     echo "      </div>\n";  // table2Col
  }

  {
     echo "      <div class=\"rounded-border\" id=\"aSelect\">\n";
     echo "        <form name='navigation' method='get'>\n";

     // ----------------
     // Date Picker

     echo "          Zeitraum der Charts<br/>\n";
     echo datePicker("", "s", $syear, $sday, $smonth);

     echo "          <select name=\"srange\">\n";
     echo "            <option value='1' "  . ($srange == 1  ? "SELECTED" : "") . ">Tag</option>\n";
     echo "            <option value='7' "  . ($srange == 7  ? "SELECTED" : "") . ">Woche</option>\n";
     echo "            <option value='31' " . ($srange == 31 ? "SELECTED" : "") . ">Monat</option>\n";
     echo "          </select>\n";
     echo "          <input type=submit value=\"Go\">";
     echo "        </form>\n";

     // -------

     echo "        <form action=" . htmlspecialchars($_SERVER["PHP_SELF"]) . " method=post>\n";
     echo "          <br/>\n";
     echo "          <button class=\"rounded-border button3\" type=submit name=action value=clearpeaks>Peaks löschen</button>\n";
     echo "        </form>\n";
     echo "      </div>\n";
  }

  $mysqli->close();

include("footer.php");
?>
