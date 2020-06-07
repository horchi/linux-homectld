<?php

include("header.php");

printHeader();

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

// ----------------
// widgets

{
   echo "    <script type=\"module\" src=\"main.js\"></script>\n";

   // select s.address as s_address, s.type as s_type, s.time as s_time, f.title, f.usrtitle from samples s left join valuefacts f on f.address = s.address and f.type = s.type where f.state = 'A' and s.time = '2020-03-27 12:19:00'

   buildIdList($_SESSION['addrsDashboard'], $sensors, $addrWhere, $ids);

   $strQueryBase = sprintf("select
                              s.address as s_address, s.type as s_type, s.time as s_time, s.value as s_value, s.text as s_text,
                              f.name as f_name, f.title as f_title, f.usrtitle as f_usrtitle, f.unit as f_unit
                              from samples s
                              join valuefacts f on f.address = s.address and f.type = s.type");

   if ($addrWhere == "")
      $strQuery = sprintf("%s where s.time = '%s'", $strQueryBase, $max);
   else
      $strQuery = sprintf("%s where (%s) and s.time = '%s'", $strQueryBase, $addrWhere, $max);

   $result = $mysqli->query($strQuery)
      or die("Error" . $mysqli->error);

   $map = new \Ds\Map();
   $i = 0;

   while ($row = $result->fetch_assoc())
   {
      $id = "0x".dechex($row['s_address']).":".$row['s_type'];
      $map->put($id, $row);

      if ($addrWhere == "")
         $ids[$i++] = $id;
   }

   echo "    <div id=\"widgetContainerOld\" class=\"widgetContainer\">\n";

   echo "        <div class=\"widget rounded-border\">\n";
   echo "          <div class=\"widget-title\">Zeit</div>";
   echo "          <div id=\"refreshTime\" class=\"widget-value\"></div>";
   echo "        </div>\n";

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

      $name = $row['f_name'];
      $value = $row['s_value'];
      $text = $row['s_text'];
      $title = $row['f_usrtitle'] != "" ? $row['f_usrtitle'] : $row['f_title'];
      $unit = prettyUnit($row['f_unit']);
      $u = $row['f_unit'];
      $address = $row['s_address'];
      $type = $row['s_type'];

      // ------------------
      // draw widgets

      if (stripos($text, "ERROR:") !== FALSE)
      {
         list($tmp, $text) = explode(":", $text, 2);
         echo "        <div class=\"widget rounded-border\">\n";
         echo "          <div class=\"widget-title\">$title</div>";
         echo "          <div id=\"widget$type$address\" class=\"widget-value widget-error\">$text</div>";
         echo "        </div>\n";
      }

      else if ($unit == '°C' || $unit == '%'|| $unit == 'V' || $unit == 'A')       // 'Volt/Ampere/Prozent/°C' als Gauge
      {
         $range = 1;
         $from = strtotime("today", time());

         $url = "href=\"#\" onclick=\"window.open('detail.php?width=1200&height=600&address=$address&type=$type&from="
            . $from . "&range=" . $range . "&chartXLines=" . $_SESSION['chartXLines'] . "&chartDiv="
            . $_SESSION['chartDiv'] . " ','_blank',"
            . "'scrollbars=yes,width=1200,height=600,resizable=yes,left=120,top=120')\"";

         echo "        <div $url id=\"widget$type$address\" class=\"widgetGauge rounded-border participation\" >\n";
         echo "          <div class=\"widget-title\">$title</div>";
         echo "          <svg class=\"widget-svg\" viewBox=\"0 0 1000 600\" preserveAspectRatio=\"xMidYMin slice\">\n";
         echo "            <path id=\"pb$type$address\"/>\n";
         echo "            <path class=\"data-arc\" id=\"pv$type$address\"/>\n";
         echo "            <path class=\"data-peak\" id=\"pb$type$address\"/>\n";
         echo "            <text id=\"value$type$address\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"500\" y=\"450\" font-size=\"140\" font-weight=\"bold\"></text>\n";
         echo "            <text id=\"sMin$type$address\" class='scale-text' text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"50\" y=\"550\"></text>\n";
         echo "            <text id=\"sMax$type$address\" class='scale-text' text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"950\" y=\"550\"></text>\n";
         echo "          </svg>\n";
         echo "        </div>\n";
      }

      else if ($text != '')                                                    // 'text' als Text anzeigen
      {
         $value = str_replace($wd_value, $wd_disp, $text);

         echo "        <div class=\"widget rounded-border\">\n";
         echo "          <div class=\"widget-title\">$title</div>";
         echo "          <div id=\"widget$type$address\" class=\"widget-value\">$value</div>";
         echo "        </div>\n";
      }

      else if ($u == 'h' || $u == 'U' || $u == 'R' || $u == 'm' || $u == 'u')  // 'value' als Text anzeigen
      {
         $value = round($value, 0);

         echo "        <div class=\"widget rounded-border\">\n";
         echo "          <div class=\"widget-title\">$title</div>";
         echo "          <div id=\"widget$type$address\" class=\"widget-value\"></div>";
         echo "        </div>\n";
      }

      else if ($type == 'DI' || $type == 'DO' || $u == '')                     // 'boolean' als symbol anzeigen
      {
         $ctrlButtons = $name == "Pool Light" && $_SESSION['poolLightColorToggle'];

         if (!$ctrlButtons)
            echo "        <div id=\"div$type$address\" class=\"widget rounded-border\" >\n";
         else
            echo "        <div class=\"widgetCtrl rounded-border\">\n";

         if (!haveLogin())
            $element =  "button type=\"button\"";
         else
            $element = "button type=\"button\" onclick=\"toggleMode($address, '$type')\"";

         echo "          <$element class=\"widget-title\">$title</$element>\n";

         if (!haveLogin())
            echo "          <button class=\"widget-main\" type=\"button\" >\n";
         else
            echo "          <button class=\"widget-main\" type=\"button\" onclick=\"toggleIo($address, '$type')\" >\n";
         echo "            <img id=\"widget$type$address\"  />\n";
         echo "          </button>\n";

         if ($ctrlButtons)
         {
            /* echo "            <div class=\"widget-ctrl1\">\n"; */
            /* echo "              <button type=submit name=action value=\"_switch_prev:$address\">\n"; */
            /* echo "                <img src=\"img/icon/left.png\" />\n"; */
            /* echo "              </button>\n"; */
            /* echo "            </div>\n"; */
            echo "            <div class=\"widget-ctrl2\">\n";
            if (!haveLogin())
               echo "              <button type=\"button\">\n";
            else
               echo "              <button type=\"button\" onclick=\"toggleIoNext($address, '$type')\">\n";
            echo "                <img src=\"img/icon/right.png\" />\n";
            echo "              </button>\n";
            echo "            </div>\n";

         }
         echo "        </div>\n";
      }
   }

   echo "  </div>\n";  // widgetContainerOld
}

$mysqli->close();

include("footer.php");
?>
