<?php

// -----------------------------
// user settings

$mysqlhost       = "localhost";
$mysqlport       = 3306;
$mysqluser       = "pool";
$mysqlpass       = "pool";
$mysqldb         = "pool";

$secretLoginToken = "dein secret login token";

// -----------------------------
// don't touch below ;-)

if (function_exists('opcache_reset'))
   opcache_reset();

?>
