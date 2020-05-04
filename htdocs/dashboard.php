<?php

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


  $action = "";

  // ------------------
  // get post

  if (isset($_POST["action"]))
  {
     if (!haveLogin())
     {
        echo "<br/><div class=\"infoError\"><b><center>Login erforderlich!</center></b></div><br/>\n";
     }
     else
     {
        $action = htmlspecialchars($_POST["action"]);

        if (strpos($action, "switch") == 1)
        {
           $resonse = "";
           list($cmd, $pin) = explode(":", $action, 2);

           if (strpos($action, "switch_mode") == 1)
           {
              $state = requestAction("getio", 3, $pin, "", $response);

              if ($state == 0)
              {
                 list($pin, $data) = explode("#", $response, 2);
                 list($value, $mode, $opt) = explode(":", $data, 3);

                 if ($opt == 3)
                    requestAction("toggle-output-mode", 3, $pin, $mode == 'auto' ? "manual" : "auto", $response);
              }
           }
           else if (strpos($action, "switch_next") == 1)
           {
              $state = requestAction("getio", 3, $pin, "", $response);

              if ($state == 0)
              {
                 list($pin, $data) = explode("#", $response, 2);
                 list($value, $mode, $opt) = explode(":", $data, 3);

                 if ($value == 0)
                    requestAction("setio", 3, $pin, "1", $response);
                 else
                    requestAction("toggleio2", 3, $pin, "", $response);
              }
           }
           else
           {
              requestAction("toggleio", 3, $pin, "", $response);
           }
        }
     }
  }

  // -------------------------
  // get last time stamp

  $result = $mysqli->query("select max(time), DATE_FORMAT(max(time),'%d. %M %Y   %H:%i') as maxPretty, " .
                        "DATE_FORMAT(max(time),'%H:%i:%S') as maxPrettyShort from samples;")
     or die("Error" . $mysqli->error);

  $row = $result->fetch_assoc();
  $max = $row['max(time)'];
  $maxPretty = $row['maxPretty'];
  $maxPrettyShort = $row['maxPrettyShort'];

  // ------------------
  // State of P4 Daemon

  $p4dstate = requestAction("p4d-state", 3, 0, "", $response);
  $load = "";

  if ($p4dstate == 0)
    list($p4dNext, $p4dVersion, $p4dSince, $load) = explode("#", $response, 4);

  $result = $mysqli->query("select * from samples where time >= CURDATE()")
     or die("Error" . $mysqli->error);
  $p4dCountDay = $result->num_rows;

  // -----------------
  // State 'flex' Box

  echo "      <div class=\"stateInfo\">\n";

  // -----------------
  // p4d State
  {
     echo "        <div class=\"rounded-border P4dInfo\" id=\"divP4dState\">\n";

     if ($p4dstate == 0)
     {
        echo  "              <div id=\"aStateOk\"><span>Pool Control Daemon ONLINE</span>   </div>\n";
        echo  "              <div><span>Läuft seit:</span>            <span>$p4dSince</span>       </div>\n";
        echo  "              <div><span>Messungen heute:</span>       <span>$p4dCountDay</span>    </div>\n";
        echo  "              <div><span>Letzte Messung:</span>        <span>$maxPrettyShort</span> </div>\n";
        echo  "              <div><span>Nächste Messung:</span>       <span>$p4dNext</span>        </div>\n";
        echo  "              <div><span>Version (p4d / webif):</span> <span>$p4dVersion / $p4WebVersion</span></div>\n";
        echo  "              <div><span>CPU-Last:</span>              <span>$load</span>           </div>\n";
     }
     else
     {
        echo  "          <div id=\"aStateFail\">ACHTUNG:<br/>Pool Control Daemon OFFLINE</div>\n";
     }

     echo "        </div>\n"; // P4dInfo
  }

  echo "      </div>\n";   // stateInfo

  // ----------------
  // widgets

  {
      // select s.address as s_address, s.type as s_type, s.time as s_time, f.title, f.usrtitle from samples s left join peaks p on p.address = s.address and p.type = s.type join valuefacts f on f.address = s.address and f.type = s.type where f.state = 'A' and s.time = '2020-03-27 12:19:00'

      buildIdList($_SESSION['addrsDashboard'], $sensors, $addrWhere, $ids);

      $strQueryBase = sprintf("select p.minv as p_min, p.maxv as p_max,
                              s.address as s_address, s.type as s_type, s.time as s_time, s.value as s_value, s.text as s_text,
                              f.name as f_name, f.title as f_title, f.usrtitle as f_usrtitle, f.unit as f_unit, f.maxscale as f_maxscale
                              from samples s left join peaks p on p.address = s.address and p.type = s.type
                              join valuefacts f on f.address = s.address and f.type = s.type");

      if ($addrWhere == "")
          $strQuery = sprintf("%s where s.time = '%s'", $strQueryBase, $max);
      else
          $strQuery = sprintf("%s where (%s) and s.time = '%s'", $strQueryBase, $addrWhere, $max);

      // syslog(LOG_DEBUG, "p4: selecting " . " '" . $strQuery . "'");

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

      echo "  <form action=" . htmlspecialchars($_SERVER["PHP_SELF"]) . " method=post>\n";
      echo "    <div class=\"widgetContainer\">\n";

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
         $scaleMax = $row['f_maxscale'];
         $address = $row['s_address'];
         $type = $row['s_type'];
         $peak = $row['p_max'];
         $mode = '';
         $opt = 0;

         // case of DO request the actual state instead of the last stored value

         if ($type == 'DO')
         {
             $state = requestAction("getio", 3, $address, "", $response);

             if ($state == 0)
             {
                 list($pin, $data) = explode("#", $response, 2);
                 list($value, $mode, $opt) = explode(":", $data, 3);

                 if ($opt == 3)
                 {
                    $modeStyle = $mode == 'auto' ? "color:darkblue;" : "color:red;";
                    $mode = $mode == 'auto' ? "" : "&nbsp;&nbsp;&nbsp;&nbsp;(manuell)";
                 }
                 else
                    $mode = "";
             }
         }

         if ($unit == '°C' || $unit == '%'|| $unit == 'V' || $unit == 'A')       // 'Volt/Ampere/Prozent/°C' als  Gauge
         {
             $scaleMax = $unit == '%' ? 100 : $scaleMax;
             $ratio = $value / $scaleMax;
             $peak = $peak / $scaleMax;

             $range = 1;
             $from = strtotime("today", time());

             $url = "href=\"#\" onclick=\"window.open('detail.php?width=1200&height=600&address=$address&type=$type&from="
                . $from . "&range=" . $range . "&chartXLines=" . $_SESSION['chartXLines'] . "&chartDiv="
                . $_SESSION['chartDiv'] . " ','_blank',"
                . "'scrollbars=yes,width=1200,height=600,resizable=yes,left=120,top=120')\"";

             echo "        <div $url class=\"widgetGauge rounded-border participation\" data-y=\"500\" data-unit=\"$unit\" data-value=\"$value\" data-peak=\"$peak\" data-ratio=\"$ratio\">\n";
             echo "          <div class=\"widget-title\">$title</div>";
             echo "          <svg class=\"widget-svg\" viewBox=\"0 0 1000 600\" preserveAspectRatio=\"xMidYMin slice\">\n";
             echo "            <path d=\"M 950 500 A 450 450 0 0 0 50 500\"></path>\n";
             echo "            <text class='_content' text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"500\" y=\"450\" font-size=\"140\" font-weight=\"bold\">$unit</text>\n";
             echo "            <text class='scale-text' text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"50\" y=\"550\">0</text>\n";
             echo "            <text class='scale-text' text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"950\" y=\"550\">$scaleMax</text>\n";
             echo "          </svg>\n";
             echo "        </div>\n";
         }
         else if ($text != '')                                                    // 'text' als Text anzeigen
         {
             $value = str_replace($wd_value, $wd_disp, $text);

             echo "        <div class=\"widget rounded-border\">\n";
             echo "          <div class=\"widget-title\">$title</div>";
             echo "          <div class=\"widget-value\">$value</div>";
             echo "        </div>\n";
         }
         else if ($u == 'h' || $u == 'U' || $u == 'R' || $u == 'm' || $u == 'u')  // 'value' als Text anzeigen
         {
             $value = round($value, 0);

             echo "        <div class=\"widget rounded-border\">\n";
             echo "          <div class=\"widget-title\">$title</div>";
             echo "          <div class=\"widget-value\">$value $unit</div>";
             echo "        </div>\n";
         }
         else if ($type == 'DI' || $type == 'DO' || $u == '')                     // 'boolean' als symbol anzeigen
         {
            if (stripos($name, "Pump") !== FALSE)
               $imagePath = $value == "1.00" ? "img/icon/pump-on.gif" : $imagePath = "img/icon/pump-off.png";
            else if (stripos($name, "UV-C") !== FALSE)
               $imagePath = $value == "1.00" ? "img/icon/uvc-on.png" : $imagePath = "img/icon/uvc-off.png";
            else if (stripos($name, "Light") !== FALSE || stripos($title, "Licht") !== FALSE)
               $imagePath = $value == "1.00" ? "img/icon/light-on.png" : $imagePath = "img/icon/light-off.png";
            else
               $imagePath = $value == "1.00" ? "img/icon/boolean-on.png" : $imagePath = "img/icon/boolean-off.png";

            $ctrlButtons = $name == "Pool Light" && $_SESSION['poolLightColorToggle'];

            if (!$ctrlButtons)
               echo "        <div class=\"widget rounded-border\">\n";
            else
               echo "        <div class=\"widgetCtrl rounded-border\">\n";

            $element = $opt == 3 ? "button type=submit name=action value=\"_switch_mode:$address\"" : "div";

            echo "          <$element class=\"widget-title\">\n";
            echo "            $title";
            echo "            <span style=\"$modeStyle\" class=\"widget-mode\">$mode</span>";
            echo "          </$element>\n";
            echo "          <button class=\"widget-main\" type=submit name=action value=\"_switch:$address\">\n";
            echo "            <img src=\"$imagePath\"/>\n";
            echo "          </button>\n";

            if ($ctrlButtons)
            {
               /* echo "            <div class=\"widget-ctrl1\">\n"; */
               /* echo "              <button type=submit name=action value=\"_switch_prev:$address\">\n"; */
               /* echo "                <img src=\"img/icon/left.png\" />\n"; */
               /* echo "              </button>\n"; */
               /* echo "            </div>\n"; */
               echo "            <div class=\"widget-ctrl2\">\n";
               echo "              <button type=submit name=action value=\"_switch_next:$address\">\n";
               echo "                <img src=\"img/icon/right.png\" />\n";
               echo "              </button>\n";
               echo "            </div>\n";

            }
            echo "        </div>\n";
         }
      }

      echo "    </form>\n";
      echo "  </div>\n";  // container
  }

  $mysqli->close();

include("footer.php");
?>
