<?php

if (!function_exists("functions_once"))
{
    function functions_once()
    {
    }

// ---------------------------------------------------------------------------
// Login
// ---------------------------------------------------------------------------

function haveLogin()
{
   if (ipInRange(getClientIp(), $_SESSION['localLoginNetmask']))
      return true;

   return false;
}

// ---------------------------------------------------------------------------
// IP In Range
// ---------------------------------------------------------------------------

function ipInRange($ip, $range)
{
   if ($range == "")
      return false;

	if (strpos($range, '/') == false)
		$range .= '/32';

	// $range is in IP/CIDR format eg 127.0.0.1/24

	list($range, $netmask) = explode( '/', $range, 2);

	$range_decimal = ip2long($range);
	$ip_decimal = ip2long($ip);
	$wildcard_decimal = pow(2, (32 - $netmask)) - 1;
	$netmask_decimal = ~ $wildcard_decimal;

	return ($ip_decimal & $netmask_decimal) == ($range_decimal & $netmask_decimal);
}

// ---------------------------------------------------------------------------
// Get Client IP
// ---------------------------------------------------------------------------

function getClientIp()
{
   if (!empty($_SERVER['HTTP_CLIENT_IP']))
      $ipAddress = $_SERVER['HTTP_CLIENT_IP'];

   elseif (!empty($_SERVER['HTTP_X_FORWARDED_FOR']))     // whether ip is from proxy
      $ipAddress = $_SERVER['HTTP_X_FORWARDED_FOR'];

   else                                                  // whether ip is from remote address
      $ipAddress = $_SERVER['REMOTE_ADDR'];

   return $ipAddress;
}

function mysqli_result($res, $row=0, $col=0)
{
   $numrows = mysqli_num_rows($res);

   if ($numrows && $row <= ($numrows-1) && $row >=0)
   {
      mysqli_data_seek($res,$row);
      $resrow = (is_numeric($col)) ? mysqli_fetch_row($res) : mysqli_fetch_assoc($res);

       if (isset($resrow[$col]))
         return $resrow[$col];
   }

   return false;
}

// ---------------------------------------------------------------------------
// Read Config Item
// ---------------------------------------------------------------------------

function readConfigItem($name, &$value, $default = "")
{
   global $mysqli;

   // new version - read direct from config table

   $result = $mysqli->query("select value from config where owner = 'poold' and name = '$name'")
         or die("Error" . $mysqli->error);

   if ($result->num_rows > 0)
     $value = mysqli_result($result, 0, "value");
   else
     $value = $default;

   return 0;
}

// ---------------------------------------------------------------------------
// Option Config Item
// ---------------------------------------------------------------------------

/*
function configOptionItem($flow, $title, $name, $value, $options, $comment = "", $attributes = "")
{
   $end = "";

   echo "          <span>$title:</span>\n";
   echo "          <span>\n";
   echo "            <select class=\"rounded-border input\" name=\"" . $name . "\" "
      . $attributes . ">\n";

   foreach (explode(" ", $options) as $option)
   {
      $opt = explode(":", $option);
      $sel = ($value == $opt[1]) ? "SELECTED" : "";

      echo "              <option value=\"$opt[1]\" " . $sel . ">$opt[0]</option>\n";
   }

   echo "            </select>\n";
   echo "          </span>\n";

   if ($comment != "")
      echo "          <span class=\"inputComment\">($comment)</span>\n";

   echo $end;
}
*/

}  // "functions_once"
?>
