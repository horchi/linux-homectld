<?php

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

$action = htmlspecialchars($_POST['action']);

if ($action == "toggleio")
{
   $response = "";
   $pin = htmlspecialchars($_POST['addr']);

   requestAction("toggleio", 3, $pin, "", $response);

   $state = requestAction("getio", 3, $pin, "", $response);

   if ($state == 0)
   {
      list($pin, $value, $mode, $opt) = explode(":", $response, 4);
      echo $value;
   }
}

else if ($action == "toggleioNext")
{
   $response = "";
   $pin = htmlspecialchars($_POST['addr']);

   requestAction("toggleio2", 3, $pin, "", $response);

   $state = requestAction("getio", 3, $pin, "", $response);

   if ($state == 0)
   {
      list($pin, $value, $mode, $opt) = explode(":", $response, 4);
      echo $value;
   }
}

else if ($action == "getImageOf")
{
   $title = htmlspecialchars($_POST['title']);
   $value = htmlspecialchars($_POST['value']);
   echo getImageOf($title, $value);
}

else if ($action == "togglemode")
{
   $pin = htmlspecialchars($_POST['addr']);

   $state = requestAction("getio", 3, $pin, "", $response);

   if ($state == 0)
   {
      list($pin, $value, $mode, $opt) = explode(":", $response, 4);

      if ($opt == 3)
      {
         $mode = $mode == 'auto' ? "manual" : "auto";
         requestAction("toggle-output-mode", 3, $pin, $mode, $response);
         echo $mode;
         return ;
      }
   }

   echo "";
}

?>
