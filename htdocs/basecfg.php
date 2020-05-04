<?php

include("header.php");

printHeader();

include("setup.php");

// -------------------------
// chaeck login

if (!haveLogin())
{
   echo "<br/><div class=\"infoError\"><b><center>Login erforderlich!</center></b></div><br/>\n";
   die("<br/>");
}

// -------------------------
// establish db connection

$mysqli = new mysqli($mysqlhost, $mysqluser, $mysqlpass, $mysqldb, $mysqlport);
$mysqli->query("set names 'utf8'");
$mysqli->query("SET lc_time_names = 'de_DE'");

// ------------------
// variables

$action = "";

// ------------------
// get post

if (isset($_POST["action"]))
   $action = htmlspecialchars($_POST["action"]);

if ($action == "mailtest")
{
   $resonse = "";

   if (sendTestMail("Test Mail", "test", $resonse))
      echo "      <br/><div class=\"info\"><b><center>Mail Test succeeded</center></div><br/>\n";
   else
      echo "      <br/><div class=\"infoError\"><b><center>Sending Mail failed '$resonse' - syslog log for further details</center></div><br/>\n";
}

else if ($action == "store")
{
   // ------------------
   // store settings

   if (isset($_POST["refreshWeb"]))
      $_SESSION['refreshWeb'] = htmlspecialchars($_POST["refreshWeb"]);

   if (isset($_POST["style"]))
      $style = htmlspecialchars($_POST["style"]);

   if (isset($_POST["addrsMain"]))
      $_SESSION['addrsMain'] = htmlspecialchars($_POST["addrsMain"]);

   if (isset($_POST["addrsMainMobile"]))
      $_SESSION['addrsMainMobile'] = htmlspecialchars($_POST["addrsMainMobile"]);

   if (isset($_POST["addrsDashboard"]))
       $_SESSION['addrsDashboard'] = htmlspecialchars($_POST["addrsDashboard"]);

   if (isset($_POST["chartStart"]))
      $_SESSION['chartStart'] = htmlspecialchars($_POST["chartStart"]);

   $_SESSION['chartXLines'] = isset($_POST["chartXLines"]) ? "1" : "0";

   if (isset($_POST["chartDiv"]))
      $_SESSION['chartDiv'] = htmlspecialchars($_POST["chartDiv"]);

   if (isset($_POST["chart1"]))
      $_SESSION['chart1'] = htmlspecialchars($_POST["chart1"]);

   if (isset($_POST["chart2"]))
      $_SESSION['chart2'] = htmlspecialchars($_POST["chart2"]);

   // ---

   if (isset($_POST["interval"]))
      $_SESSION['interval'] = htmlspecialchars($_POST["interval"]);

   $_SESSION['mail'] = isset($_POST["mail"]) ? "1" : "0";

   if (isset($_POST["stateMailTo"]))
      $_SESSION['stateMailTo'] = htmlspecialchars($_POST["stateMailTo"]);

   if (isset($_POST["errorMailTo"]))
      $_SESSION['errorMailTo'] = htmlspecialchars($_POST["errorMailTo"]);

   if (isset($_POST["mailScript"]))
      $_SESSION['mailScript'] = htmlspecialchars($_POST["mailScript"]);

   if (isset($_POST["user"]))
      $_SESSION['user'] = htmlspecialchars($_POST["user"]);

   if (isset($_POST["passwd1"]) && $_POST["passwd1"] != "")
      $_SESSION['passwd1'] = md5(htmlspecialchars($_POST["passwd1"]));

   if (isset($_POST["passwd2"]) && $_POST["passwd2"] != "")
      $_SESSION['passwd2'] = md5(htmlspecialchars($_POST["passwd2"]));

   if (isset($_POST["webUrl"]))
      $_SESSION['webUrl'] = (substr($_SESSION['webUrl'],0,7) == "http://") ?  htmlspecialchars($_POST["webUrl"]) : htmlspecialchars("http://" . $_POST["webUrl"]);

   if (isset($_POST["tPoolMax"]))
      $_SESSION['tPoolMax'] = htmlspecialchars($_POST["tPoolMax"]);

   if (isset($_POST["tSolarDelta"]))
       $_SESSION['tSolarDelta'] = htmlspecialchars($_POST["tSolarDelta"]);

   if (isset($_POST["w1AddrPool"]))
       $_SESSION['w1AddrPool'] = htmlspecialchars($_POST["w1AddrPool"]);

   if (isset($_POST["w1AddrSolar"]))
       $_SESSION['w1AddrSolar'] = htmlspecialchars($_POST["w1AddrSolar"]);

   if (isset($_POST["w1AddrSuctionTube"]))
       $_SESSION['w1AddrSuctionTube'] = htmlspecialchars($_POST["w1AddrSuctionTube"]);

   if (isset($_POST["w1AddrAir"]))
       $_SESSION['w1AddrAir'] = htmlspecialchars($_POST["w1AddrAir"]);

   $_SESSION['poolLightColorToggle'] = isset($_POST["poolLightColorToggle"]) ? "1" : "0";
   $_SESSION['invertDO'] = isset($_POST["invertDO"]) ? "1" : "0";
   $_SESSION['filterPumpTimes'] = toTimeRangesString("FilterPumpTimes", $_POST);
   $_SESSION['uvcLightTimes'] = toTimeRangesString("uvcLightTimes", $_POST);
   $_SESSION['poolLightTimes'] = toTimeRangesString("poolLightTimes", $_POST);

   if (isset($_POST["aggregateHistory"]))
       $_SESSION['aggregateHistory'] = htmlspecialchars($_POST["aggregateHistory"]);

   if (isset($_POST["aggregateInterval"]))
      $_SESSION['aggregateInterval'] = htmlspecialchars($_POST["aggregateInterval"]);

   if (isset($_POST["hassMqttUrl"]))
      $_SESSION['hassMqttUrl'] = htmlspecialchars($_POST["hassMqttUrl"]);

   // ------------------
   // write settings to config

   applyColorScheme($style);

   writeConfigItem("refreshWeb", $_SESSION['refreshWeb']);
   writeConfigItem("addrsMain", $_SESSION['addrsMain']);
   writeConfigItem("addrsMainMobile", $_SESSION['addrsMainMobile']);
   writeConfigItem("addrsDashboard", $_SESSION['addrsDashboard']);

   writeConfigItem("chartStart", $_SESSION['chartStart']);
   writeConfigItem("chartDiv", $_SESSION['chartDiv']);
   writeConfigItem("chartXLines", $_SESSION['chartXLines']);
   writeConfigItem("chart1", $_SESSION['chart1']);
   writeConfigItem("chart2", $_SESSION['chart2']);

   writeConfigItem("interval", $_SESSION['interval']);
   writeConfigItem("mail", $_SESSION['mail']);
   writeConfigItem("stateMailTo", $_SESSION['stateMailTo']);
   writeConfigItem("errorMailTo", $_SESSION['errorMailTo']);
   writeConfigItem("mailScript", $_SESSION['mailScript']);
   writeConfigItem("webUrl", $_SESSION['webUrl']);

   writeConfigItem("aggregateHistory", $_SESSION['aggregateHistory']);
   writeConfigItem("aggregateInterval", $_SESSION['aggregateInterval']);
   writeConfigItem("hassMqttUrl", $_SESSION['hassMqttUrl']);

   if ($_POST["passwd2"] != "")
   {
      if (htmlspecialchars($_POST["passwd1"]) ==  htmlspecialchars(($_POST["passwd2"])))
      {
         writeConfigItem("user", $_SESSION['user']);
         writeConfigItem("passwd", $_SESSION['passwd1']);
         echo "      <br/><div class=\"info\"><b><center>Passwort gespeichert</center></div><br/>\n";
      }
      else
      {
         echo "      <br/><div class=\"infoError\"><b><center>Passwort stimmt nicht überein</center></div><br/>\n";
      }
   }

   writeConfigItem("filterPumpTimes", $_SESSION['filterPumpTimes']);
   writeConfigItem("uvcLightTimes", $_SESSION['uvcLightTimes']);
   writeConfigItem("poolLightTimes", $_SESSION['poolLightTimes']);

   writeConfigItem("tPoolMax", $_SESSION['tPoolMax']);
   writeConfigItem("tSolarDelta", $_SESSION['tSolarDelta']);
   writeConfigItem("w1AddrPool", $_SESSION['w1AddrPool']);
   writeConfigItem("w1AddrSolar", $_SESSION['w1AddrSolar']);
   writeConfigItem("w1AddrSuctionTube", $_SESSION['w1AddrSuctionTube']);
   writeConfigItem("w1AddrAir", $_SESSION['w1AddrAir']);
   writeConfigItem("poolLightColorToggle", $_SESSION['poolLightColorToggle']);
   writeConfigItem("invertDO", $_SESSION['invertDO']);

   requestAction("apply-config", 3, 0, "", $res);
   echo "<div class=\"info\"><b><center>Einstellungen gespeichert</center></b></div>";
}

// ------------------
// setup form

echo "      <form action=" . htmlspecialchars($_SERVER["PHP_SELF"]) . " method=post>\n";
echo "      <div class=\"menu\">\n";
echo "        <button class=\"rounded-border button3\" type=submit name=action value=store>Speichern</button>\n";
echo "      </div>\n";
echo "      <br/>\n";

// ------------------------
// setup items ...

// --------------------

echo "        <div class=\"rounded-border inputTableConfig\">\n";
seperator("Web Interface", 0);
echo "       </div>\n";
echo "        <div class=\"rounded-border inputTableConfig\">\n";
seperator("Allgemein", 0, "seperatorTitle2");
colorSchemeItem(3, "Farbschema");
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("Ansicht 'Aktuell/Dashboard'", 0, "seperatorTitle2");
configNumItem(3, "Seite aktualisieren", "refreshWeb", $_SESSION['refreshWeb'], "", 0, 7200);
configStrItem(3, "Sensoren", "addrsMain", $_SESSION['addrsMain'], "", 250);
configStrItem(3, "Sensoren Mobile Device", "addrsMainMobile", $_SESSION['addrsMainMobile'], "", 250);
configStrItem(3, "Sensoren Dashboard", "addrsDashboard", $_SESSION['addrsDashboard'], "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'", 250);
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("Charts", 0, "seperatorTitle2");
configNumItem(3, "Chart Zeitraum (Tage)", "chartStart", $_SESSION['chartStart'], "Standardzeitraum der Chartanzeige (seit x Tagen bis heute)", 1, 7);
configBoolItem(3, "Senkrechte Hilfslinien", "chartXLines", $_SESSION['chartXLines'], "");
configOptionItem(3, "Linien-Abstand der Y-Achse", "chartDiv", $_SESSION['chartDiv'], "klein:15 mittel:25 groß:45", "");
configStrItem(3, "Chart 1", "chart1", $_SESSION['chart1'], "", 250);
configStrItem(3, "Chart 2", "chart2", $_SESSION['chart2'], "Komma getrennte Liste aus ID:Typ siehe 'Aufzeichnung'", 250);
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("Login", 0, "seperatorTitle2");
configStrItem(3, "User", "user", $_SESSION['user'], "", 350);
configStrItem(3, "Passwort", "passwd1", "", "", 350, "", true);
configStrItem(3, "Wiederholen", "passwd2", "", "", 350, "", true);
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("poold Konfiguration", 0);
echo "       </div>\n";
echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("Mail Benachrichtigungen", 0, "seperatorTitle2");
$ro = ($_SESSION['mail']) ? "\"" : "; background-color:#ddd;\" readOnly=\"true\"";
configBoolItem(3, "Mail Benachrichtigung", "mail\" onClick=\"disableContent('htM',this); readonlyContent('Mail',this)", $_SESSION['mail'], "Mail Benachrichtigungen aktivieren/deaktivieren");
configStrItem(3, "Status Mail Empfänger", "stateMailTo\" id=\"Mail1", $_SESSION['stateMailTo'], "Komma separierte Empfängerliste", 500, $ro);
configStrItem(3, "Fehler Mail Empfänger", "errorMailTo\" id=\"Mail2", $_SESSION['errorMailTo'], "Komma separierte Empfängerliste", 500, $ro);
configStrItem(3, "poold sendet Mails über das Skript", "mailScript\" id=\"Mail4", $_SESSION['mailScript'], "", 400, $ro);
configStrItem(3, "URL der Visualisierung", "webUrl\" id=\"Mail5", $_SESSION['webUrl'], "kann mit %weburl% in die Mails eingefügt werden", 350, $ro);
echo "        <button class=\"rounded-border button3\" type=submit name=action value=mailtest>Test Mail</button>\n";
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("Steuerung", 0, "seperatorTitle2");
configFloatItem(3, "Pool max Temperatur", "tPoolMax", $_SESSION['tPoolMax'], "", 15, 35);
configFloatItem(3, "Einschaltdifferenz Solarpumpe", "tSolarDelta", $_SESSION['tSolarDelta'], "", 0, 10);
echo "       <br/>\n";
configTimeRangesItem(3, "Zeiten Filter Pumpe", "FilterPumpTimes", $_SESSION['filterPumpTimes'], "[hh:mm] - [hh:mm]");
echo "       <br/>\n";
configTimeRangesItem(3, "Zeiten UV-C Licht", "uvcLightTimes", $_SESSION['uvcLightTimes'], "[hh:mm] - [hh:mm], wird nur angeschaltet wenn auch die Filterpumpe läuft!");
echo "       <br/>\n";
configTimeRangesItem(3, "Zeiten Pool Licht", "poolLightTimes", $_SESSION['poolLightTimes'], "[hh:mm] - [hh:mm]");
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("IO Setup", 0, "seperatorTitle2");
configStrItem(3, "Adresse Fühler Temperatur Luft", "w1AddrAir", $_SESSION['w1AddrAir'], "");
configStrItem(3, "Adresse Fühler Temperatur Pool", "w1AddrPool", $_SESSION['w1AddrPool'], "");
configStrItem(3, "Adresse Fühler Temperatur Kollektor", "w1AddrSolar", $_SESSION['w1AddrSolar'], "");
configStrItem(3, "Adresse Fühler Temperatur Saugleitung", "w1AddrSuctionTube", $_SESSION['w1AddrSuctionTube'], "");
configBoolItem(3, "Digitalaugänge invertieren", "invertDO", $_SESSION['invertDO'], "");
configBoolItem(3, "Pool Licht Farb-Toggel", "poolLightColorToggle", $_SESSION['poolLightColorToggle'], "");
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("Sonstiges", 0, "seperatorTitle2");
configNumItem(3, "Intervall der Aufzeichung", "interval", $_SESSION['interval'], "Datenbank Aufzeichung [s]");
configStrItem(3, "Home Assistant MQTT Url ", "hassMqttUrl", $_SESSION['hassMqttUrl'], "Optional. Beispiel: 'tcp://127.0.0.1:1883'");
echo "       </div>\n";

echo "      <div class=\"rounded-border inputTableConfig\">\n";
seperator("Aggregation", 0, "seperatorTitle2");
configNumItem(3, "Historie [Tage]", "aggregateHistory", $_SESSION['aggregateHistory'], "history for aggregation in days (default 0 days -> aggegation turned OFF)", 0, 3650);
configNumItem(3, "Intervall [m]", "aggregateInterval", $_SESSION['aggregateInterval'], "aggregation interval in minutes - 'one sample per interval will be build'", 5, 60);
echo "       </div>\n";

echo "      </form>\n";

//requestAction("syslog", 3, 0, "", $res);
//echo "<br/><br/>Syslog: <br/><div>$res</div>\n";

include("footer.php");

// ---------------------------------------------------------------------------
// Color Scheme
// ---------------------------------------------------------------------------

function colorSchemeItem($new, $title)
{
   $actual = readlink("stylesheet.css");

   $end = htmTags($new);
   echo "          <span>$title:</span>\n";
   echo "          <span>\n";
   echo "            <select class=\"rounded-border input\" name=\"style\">\n";

   foreach (glob("stylesheet-*.css") as $filename)
   {
      $sel = $actual == $filename ? "SELECTED" : "";
      $tp = substr(strstr($filename, ".", true), 11);
      echo "              <option value='$tp' " . $sel . ">$tp</option>\n";
   }

   echo "            </select>\n";
   echo "          </span>\n";
   echo $end;
}

function applyColorScheme($style)
{
   $target = "stylesheet-$style.css";

   if ($target != readlink("stylesheet.css"))
   {
   	  if (!readlink("stylesheet.css"))
   	    symlink($target, "stylesheet.css");

      if (!unlink("stylesheet.css") || !symlink($target, "stylesheet.css"))
      {
         $err = error_get_last();
         echo "      <br/><br/>Fehler beim Löschen/Anlegen des Links 'stylesheet.css'<br\>\n";

         if (strstr($err['message'], "Permission denied"))
            echo "      <br/>Rechte der Datei prüfen!<br/><br\>\n";
         else
            echo "      <br/>Fehler: '" . $err['message'] . "'<br/>\n";
      }
      else
         echo '<script>parent.window.location.reload();</script>';
   }
}

?>
