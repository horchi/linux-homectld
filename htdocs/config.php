<?php

// -----------------------------
// user settings

$mysqlhost       = "localhost";
$mysqluser       = "pool";
$mysqlpass       = "pool";
$mysqldb         = "pool";

$cache_dir       = "pChart/cache";
$chart_fontpath  = "pChart/fonts";
$debug           = 0;

$wd_value        = array("Monday", "Tuesday",  "Wednesday", "Thursday",   "Friday",  "Saturday", "Sunday");
$wd_disp         = array("Montag", "Dienstag", "Mittwoch",  "Donnerstag", "Freitag", "Samstag",  "Sonntag");

// -----------------------------
// don't touch below ;-)

if (function_exists('opcache_reset'))
   opcache_reset();

?>
