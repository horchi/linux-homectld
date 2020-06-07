<?php

include("config.php");
include("functions.php");

$mysqli = new mysqli($mysqlhost, $mysqluser, $mysqlpass, $mysqldb, $mysqlport);

if (mysqli_connect_error())
{
   die('Connect Error (' . mysqli_connect_errno() . ') '
       . mysqli_connect_error() . ". Can't connect to " . $mysqlhost . ' at ' . $mysqlport);
}

$mysqli->query("set names 'utf8'");
$mysqli->query("SET lc_time_names = 'de_DE'");

$action = htmlspecialchars($_POST['action']);

if ($action == "gettoken")
{
   // writeConfigItem("WSLoginToken", $secretLoginToken);
   readConfigItem("localLoginNetmask", $_SESSION['localLoginNetmask']);

   if (haveLogin())
      echo $secretLoginToken;
   else
      echo "NO LOGIN";
}

?>
