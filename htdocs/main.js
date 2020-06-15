
import WebSocketClient from "./websocket.js"

var isActive = null;
var socket = null;
var config = {};
var daemonState = {};
var lastUpdate = "";

function connectWebSocket()
{
   var useUrl = "";

   if (location.hostname.indexOf("192.168.200.142") != -1)
      useUrl = "ws://" + location.hostname + ":61109";
   else
      useUrl = "ws://" + location.hostname + "/pool/ws";

   socket = new WebSocketClient({
      // url: "ws://" + location.hostname + ":61109",
      url: useUrl,
      protocol: "pool",
      autoReconnectInterval: 2000,
      onopen: () => {
         console.log("socket opened :)");
         if (isActive === null)     // wurde beim Schliessen auf null gesetzt
         {
            var token = localStorage.getItem('token');
            if (!token || token == null) token = "";

            daemonState.state = -1;
            // console.log("LOGIN: stored token is: " + localStorage.getItem('token'));

            socket.send({ "event" : "login", "object" :
                          { "type" : "active", "token" : token } });

            // request some data after login

            if (document.getElementById("syslogContainer") != null)
               socket.send({ "event" : "syslog", "object" : "" });
            else if (document.getElementById("configContainer") != null)
               socket.send({ "event" : "configdetails", "object" : "" });
            else if (document.getElementById("ioSetupContainer") != null)
               socket.send({ "event" : "iosettings", "object" : "" });
            else if (document.getElementById("chart") != null)
               initCharts();
         }
      }, onclose: () => {
         isActive = null;           // auf null setzen, dass ein neues login aufgerufen wird
      }, onmessage: (msg) => {
         dispatchMessage(msg.data)
      }
   });

   if (!socket)
      return !($el.innerHTML = "Your Browser will not support Websockets!");
}

function dispatchMessage(message)
{
   var jMessage = JSON.parse(message);
   var event = jMessage.event;
   var rootList = document.getElementById("listContainer");
   var rootDashboard = document.getElementById("widgetContainer");
   var rootConfig = document.getElementById("configContainer");
   var rootIoSetup = document.getElementById("ioSetupContainer");
   var rootChart = document.getElementById("chart");

   var d = new Date();

   console.log("got event: " + event);

   if ((event == "update" || event == "all") && rootDashboard)
   {
      lastUpdate = d.toLocaleTimeString();
      updateDashboard(jMessage.object);
   }
   if ((event == "update" || event == "all") && rootList)
   {
      lastUpdate = d.toLocaleTimeString();
      updateList(jMessage.object);
   }
   else if (event == "init" && rootDashboard)
   {
      lastUpdate = d.toLocaleTimeString();
      initDashboard(jMessage.object, rootDashboard);
      updateDashboard(jMessage.object);
   }
   else if (event == "init" && rootList)
   {
      lastUpdate = d.toLocaleTimeString();
      initList(jMessage.object, rootList);
      updateList(jMessage.object);
   }
   else if (event == "config")
   {
      updateConfig(jMessage.object)
   }
   else if (event == "configdetails" && rootConfig)
   {
      initConfig(jMessage.object, rootConfig)
   }
   else if (event == "daemonstate")
   {
      daemonState = jMessage.object;
   }
   else if (event == "syslog")
   {
      showSyslog(jMessage.object);
   }
   else if (event == "token")
   {
      // console.log("got token: " + jMessage.object.value);
      localStorage.setItem('token', jMessage.object.value);
      window.location.replace("index.html");
   }
   else if (event == "valuefacts" && rootIoSetup)
   {
      initIoSetup(jMessage.object, rootIoSetup);
   }
   else if (event == "chartdata" && rootChart)
   {
      drawCharts(jMessage.object, rootChart);
   }

   // console.log("event: " + event + " dispatched");
}

function showSyslog(log)
{
   var root = document.getElementById("syslogContainer");
   root.innerHTML = log.lines.replace(/(?:\r\n|\r|\n)/g, '<br>');
}

function updateConfig(aConfig)
{
   config = aConfig;
}

function initConfig(configuration, root)
{
   var lastCat = "";

   // console.log(JSON.stringify(configuration, undefined, 4));

   configuration.sort(function(a, b) {
      return a.category.localeCompare(b.category);
   });

   for (var i = 0; i < configuration.length; i++)
   {
      var item = configuration[i];
      var html = "";

      if (lastCat != item.category)
      {
         if (i) html += "<br/>";
         html += "<div class=\"rounded-border seperatorTitle1\">" + item.category + "</div>";
         var elem = document.createElement("div");
         elem.innerHTML = html;
         root.appendChild(elem);
         html = "";
         lastCat = item.category;
      }

      html += "    <span>" + item.title + ":</span>\n";

      switch (item.type) {
      case 0:     // integer
         html += "    <span><input id=\"input_" + item.name + "\" class=\"rounded-border inputNum\" type=\"number\" value=\"" + item.value + "\"/></span>\n";
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 1:     // number (float)
         html += "    <span><input id=\"input_" + item.name + "\" class=\"rounded-border inputFloat\" type=\"number\" step=\"0.1\" value=\"" + item.value + "\"/></span>\n";
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 2:     // string

         html += "    <span><input id=\"input_" + item.name + "\" class=\"rounded-border input\" type=\"text\" value=\"" + item.value + "\"/></span>\n";
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 3:     // boolean
         html += "    <span><input id=\"checkbox_" + item.name + "\" class=\"rounded-border input\" style=\"width:auto;\" type=\"checkbox\" " + (item.value == 1 ? "checked" : "") + "/></span>\n";
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 4:     // range
         var n = 0;

         if (item.value != "")
         {
            var ranges = item.value.split(",");

            for (n = 0; n < ranges.length; n++)
            {
               var range = ranges[n].split("-");
               var nameFrom = item.name + n + "From";
               var nameTo = item.name + n + "To";

               if (!range[1]) range[1] = "";
               if (n > 0) html += "  <span/>  </span>\n";
               html += "   <span>\n";
               html += "     <input id=\"range_" + nameFrom + "\" type=\"text\" class=\"rounded-border inputTime\" value=\"" + range[0] + "\"/> -";
               html += "     <input id=\"range_" + nameTo + "\" type=\"text\" class=\"rounded-border inputTime\" value=\"" + range[1] + "\"/>\n";
               html += "   </span>\n";
               html += "   <span></span>\n";
            }
         }

         var nameFrom = item.name + n + "From";
         var nameTo = item.name + n + "To";

         if (n > 0) html += "  <span/>  </span>\n";
         html += "  <span>\n";
         html += "    <input id=\"range_" + nameFrom + "\" value=\"\" type=\"text\" class=\"rounded-border inputTime\" /> -";
         html += "    <input id=\"range_" + nameTo + "\" value=\"\" type=\"text\" class=\"rounded-border inputTime\" />\n";
         html += "  </span>\n";
         html += "  <span class=\"inputComment\">" + item.descrtiption + "</span>\n";

         break;
      }

      var elem = document.createElement("div");
      elem.innerHTML = html;
      root.appendChild(elem);
   }
}

function initIoSetup(valueFacts, root)
{
   // console.log(JSON.stringify(valueFacts, undefined, 4));

   for (var i = 0; i < valueFacts.length; i++)
   {
      var item = valueFacts[i];
      var root = null;

      var usrtitle = item.usrtitle != null ? item.usrtitle : "";
      var html = "<td>" + item.title + "</td>";
      html += "<td class=\"tableMultiColCell\"><input class=\"rounded-border inputSetting\" type=\"text\" value=\"" + usrtitle + "\"/></td>";
      if (item.type != "DO")
         html += "<td class=\"tableMultiColCell\"><input class=\"rounded-border inputSetting\" type=\"number\" value=\"" + item.scalemax + "\"/></td>";
      else
         html += "<td class=\"tableMultiColCell\"></td>";
      html += "<td style=\"text-align:center;\">" + item.unit + "</td>";
      html += "<td><input class=\"rounded-border inputSetting\" type=\"checkbox\" value=\"1:SP\" checked /></td>";
      html += "<td>0x" + item.address.toString(16) + ":" + item.type + "</td>";

      switch (item.type) {
         case 'DO': root = document.getElementById("ioDigitalOut"); break
         case 'W1': root = document.getElementById("ioOneWire");    break
         case 'SP': root = document.getElementById("ioOther");      break
      }

      if (root != null)
      {
         var elem = document.createElement("tr");
         elem.innerHTML = html;
         root.appendChild(elem);
      }
   }
}

function initList(widgets, root)
{
   if (!widgets)
   {
      console.log("Faltal: Missing payload!");
      return;
   }

   // deamon state

   var rootState = document.getElementById("stateContainer");
   var html = "";

   if (daemonState.state != null && daemonState.state == 0)
   {
      html +=  "              <div id=\"aStateOk\"><span>Pool Control ONLINE</span></div><br/>\n";
      html +=  "              <div><span>Läuft seit:</span><span>" + daemonState.runningsince + "</span>       </div>\n";
      html +=  "              <div><span>Version:</span> <span>" + daemonState.version + "</span></div>\n";
      html +=  "              <div><span>CPU-Last:</span><span>" + daemonState.average0 + " " + daemonState.average1 + " "  + daemonState.average2 + " " + "</span>           </div>\n";
   }
   else
   {
      html += "          <div id=\"aStateFail\">ACHTUNG:<br/>Pool Control OFFLINE</div>\n";
   }

   rootState.innerHTML = "";
   var elem = document.createElement("div");
   elem.className = "rounded-border daemonInfo";
   elem.innerHTML = html;
   rootState.appendChild(elem);

   // clean page content

   root.innerHTML = "";
   var elem = document.createElement("div");
   elem.className = "widget rounded-border";
   elem.innerHTML = "<br/><center id=\"refreshTime\">Messwerte von " + lastUpdate + "</center>";
   root.appendChild(elem);

   // build page content

   for (var i = 0; i < widgets.length; i++)
   {
      var html = "";
      var widget = widgets[i];

      html += "<span><a class=\"tableButton\" >" + widget.title + "</a></span>";

      var id = "id=\"widget" + widget.type + widget.address + "\"";

      if (widget.widgettype == 1 || widget.widgettype == 3)
      {
         html += "<span " + id + ">" + widget.value.toFixed(2) + "&nbsp;" + widget.unit;
         html += "&nbsp; <p style=\"display:inline;font-size:12px;font-style:italic;\">(" + widget.peak.toFixed(2) + ")</p>";
         html += "</span>";
      }
      else if (widget.widgettype == 0)
         html += "<span " + id + ">" + (widget.value ? "An" : "Aus") + "</span>";
      else
         html += "<span " + id + ">" + widget.value.toFixed(0) + "</span>";

      var elem = document.createElement("div");
      elem.className = "mainrow";
      elem.innerHTML = html;
      root.appendChild(elem);
   }
}

function updateList(sensors)
{
   document.getElementById("refreshTime").innerHTML = "Messwerte von " + lastUpdate;

   for (var i = 0; i < sensors.length; i++)
   {
      var sensor = sensors[i];
      var id = "#widget" + sensor.type + sensor.address;

      if (sensor.widgettype == 1 || sensor.widgettype == 3)
      {
         $(id).html(sensor.value.toFixed(2) + "&nbsp;" + sensor.unit +
                    "&nbsp; <p style=\"display:inline;font-size:12px;font-style:italic;\">(" + sensor.peak.toFixed(2) + ")</p>");
      }
      else if (sensor.widgettype == 0)
         $(id).html(sensor.value ? "An" : "Aus");
      else
         $(id).html(sensor.value.toFixed(0));

      // console.log(i + ") " + sensor.widgettype + " : " + sensor.title + " / " + sensor.value + "(" + id + ")");
   }
}

function initDashboard(widgets, root)
{
   if (!widgets)
   {
      console.log("Faltal: Missing payload!");
      return;
   }

   // clean page content

   root.innerHTML = "";
   var elem = document.createElement("div");
   elem.className = "widget rounded-border";
   elem.innerHTML = "<div class=\"widget-title\">Aktualisiert</div>\n<div id=\"refreshTime\" class=\"widget-value\"></div>";
   root.appendChild(elem);

   // build page content

   for (var i = 0; i < widgets.length; i++)
   {
      var widget = widgets[i];

      switch(widget.widgettype) {
      case 0:           // Symbol
         var html = "";
         var ctrlButtons = widget.name == "Pool Light" && config.poolLightColorToggle == 1;

         html += "  <button class=\"widget-title\" type=\"button\" onclick=\"toggleMode(" + widget.address + ", '" + widget.type + "')\">" + widget.title + "</button>\n";
         html += "  <button class=\"widget-main\" type=\"button\" onclick=\"toggleIo(" + widget.address + ",'" + widget.type + "')\" >\n";
         html += "    <img id=\"widget" + widget.type + widget.address + "\" />\n";
         html += "   </button>\n";

         if (ctrlButtons)
         {
            html += "  <div class=\"widget-ctrl2\">\n";
            html += "    <button type=\"button\" onclick=\"toggleIoNext(" + widget.address + ",'" + widget.type + "')\">\n";
            html += "      <img src=\"img/icon/right.png\" />\n";
            html += "    </button>\n";
            html += "  </div>\n";
         }

         html += "<div id=\"progress" + widget.type + widget.address + "\" class=\"widget-progress\">";
         html += "   <div id=\"progressBar" + widget.type + widget.address + "\" class=\"progress-bar\" style=\"visible\"></div>";
         html += "</div>";

         var elem = document.createElement("div");

         if (ctrlButtons)
            elem.className = "widgetCtrl rounded-border";
         else
            elem.className = "widget rounded-border";

         elem.setAttribute("id", "div" + widget.type + widget.address);
         elem.innerHTML = html;
         root.appendChild(elem);

         document.getElementById("progress" + widget.type + widget.address).style.visibility = "hidden";

         break;
      case 1:           // Gauge
         var html = "";

         html += "  <div class=\"widget-title\">" + widget.title + "</div>";
         html += "  <svg class=\"widget-svg\" viewBox=\"0 0 1000 600\" preserveAspectRatio=\"xMidYMin slice\">\n";
         html += "    <path id=\"pb" + widget.type + widget.address + "\"/>\n";
         html += "    <path class=\"data-arc\" id=\"pv" + widget.type + widget.address + "\"/>\n";
         html += "    <path class=\"data-peak\" id=\"pp" + widget.type + widget.address + "\"/>\n";
         html += "    <text id=\"value" + widget.type + widget.address + "\" class=\"gauge-value\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"500\" y=\"450\" font-size=\"140\" font-weight=\"bold\"></text>\n";
         html += "    <text id=\"sMin" + widget.type + widget.address + "\" class=\"scale-text\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"50\" y=\"550\"></text>\n";
         html += "    <text id=\"sMax" + widget.type + widget.address + "\" class=\"scale-text\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"950\" y=\"550\"></text>\n";
         html += "  </svg>\n";

         var elem = document.createElement("div");
         elem.className = "widgetGauge rounded-border participation";
         elem.setAttribute("id", "widget" + widget.type + widget.address);
         elem.setAttribute("href", "#");

         var start = new Date();  start.setHours(0,0,0,0);
         var url = "window.open('detail.php?width=1200&height=600&address=" + widget.address + "&type=" + widget.type + "&from="
             + Math.round(start/1000) + "&range=1&chartXLines=1&chartDiv=25 ','_blank',"
             + "'scrollbars=yes,width=1200,height=600,resizable=yes,left=120,top=120')";

         elem.setAttribute("onclick", url);

         elem.innerHTML = html;
         root.appendChild(elem);

         break;

      default:   // type 2(Text), 3(value), ...
         var html = "";
         html += "<div class=\"widget-title\">" + widget.title + "</div>\n";
         html += "<div id=\"widget" + widget.type + widget.address + "\" class=\"widget-value\"></div>\n";

         var elem = document.createElement("div");
         elem.className = "widget rounded-border";
         elem.innerHTML = html;
         root.appendChild(elem);

         break;
      }
   }
}

function updateDashboard(sensors)
{
   // console.log("updateDashboard");

   document.getElementById("refreshTime").innerHTML = lastUpdate;

   if (sensors)
   {
      for (var i = 0; i < sensors.length; i++)
      {
         var sensor = sensors[i];

         if (sensor.widgettype == 0)         // Symbol
         {
            var modeStyle = sensor.options == 3 && sensor.mode == 'manual' ? "background-color: #a27373;" : "";
            $("#widget" + sensor.type + sensor.address).attr("src", sensor.image);
            $("#div" + sensor.type + sensor.address).attr("style", modeStyle);

            document.getElementById("progress" + sensor.type + sensor.address).style.visibility = (sensor.next == 0) ? "hidden" : "visible";

            if (sensor.mode == "auto" && sensor.next > 0) {
               var pWidth = 100;
               var s = sensor;
               var id = sensor.type + sensor.address;
               var iid = setInterval(progress, 200);

               function progress() {
                  if (pWidth <= 0) {
                     clearInterval(iid);
                  } else {
                     var d = new Date();
                     pWidth = 100 - ((d/1000 - s.last) / ((s.next - s.last) / 100));
                     document.getElementById("progressBar" + id).style.width = pWidth + "%";
                  }
               }
            }
         }
         else if (sensor.widgettype == 1)    // Gauge
         {
            var elem = $("#widget" + sensor.type + sensor.address);
            var value = sensor.value.toFixed(2);
            var scaleMax = !sensor.scalemax || sensor.unit == '%' ? 100 : sensor.scalemax.toFixed(0);
            var scaleMin = value >= 0 ? "0" : Math.ceil(value / 5) * 5 - 5;

            if (scaleMax < Math.ceil(value))       scaleMax = value;
            if (scaleMax < Math.ceil(sensor.peak)) scaleMax = sensor.peak.toFixed(0);

            $("#sMin" + sensor.type + sensor.address).text(scaleMin);
            $("#sMax" + sensor.type + sensor.address).text(scaleMax);
            $("#value" + sensor.type + sensor.address).text(value + " " + sensor.unit);

            var ratio = (value - scaleMin) / (scaleMax - scaleMin);
            var peak = (sensor.peak.toFixed(2) - scaleMin) / (scaleMax - scaleMin);

            $("#pb" + sensor.type + sensor.address).attr("d", "M 950 500 A 450 450 0 0 0 50 500");
            $("#pv" + sensor.type + sensor.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, -90, ratio * 180.0 - 90));
            $("#pp" + sensor.type + sensor.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, peak * 180.0 - 91, peak * 180.0 - 90));

            // console.log(sensor.name + " : " + value + " : "  + ratio + " : " + peak + " : " + scaleMin + " : " + scaleMax);
         }
         else if (sensor.widgettype == 2)      // Text
         {
            $("#widget" + sensor.type + sensor.address).html(sensor.text);
         }
         else if (sensor.widgettype == 3)      // plain value
         {
            $("#widget" + sensor.type + sensor.address).html(value + " " + sensor.unit);
         }

         // console.log(i + ": " + sensor.name + " / " + sensor.title);
      }
   }
}

window.toggleMode = function(address, type)
{
   socket.send({ "event": "togglemode", "object":
                 { "address": address,
                   "type": type }
               });
}

window.toggleIo = function(address, type)
{
   socket.send({ "event": "toggleio", "object":
                 { "address": address,
                   "type": type }
               });
}

window.toggleIoNext = function(address, type)
{
   socket.send({ "event": "toggleionext", "object":
                 { "address": address,
                   "type": type }
               });
}

function toTimeRangesString(base)
{
   var times = "";

   for (var i = 0; i < 10; i++) {
      var id = "#" + base + i;

      if ($(id + "From") && $(id + "From").val() && $(id + "To") && $(id + "To").val()) {
         if (times != "") times += ",";
         times += $(id + "From").val() + "-" + $(id + "To").val();
      }
      else {
         break;
      }
   }

   return times;
}

window.storeConfig = function()
{
   var jsonObj = {};
   var rootConfig = document.getElementById("configContainer");

   console.log("storeSettings");

   var elements = rootConfig.querySelectorAll("[id^='input_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      jsonObj[name] = elements[i].value;
   }

   var elements = rootConfig.querySelectorAll("[id^='checkbox_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      jsonObj[name] = (elements[i].checked ? "1" : "0");
   }

   var elements = rootConfig.querySelectorAll("[id^='range_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      if (name.match("0From$")) {
         name = name.substring(0, name.indexOf("0From"));
         jsonObj[name] = toTimeRangesString("range_" + name);
         console.log("value: " + jsonObj[name]);
      }
   }

   // console.log(JSON.stringify(jsonObj, undefined, 4));

   socket.send({ "event" : "storeconfig", "object" : jsonObj });

   document.getElementById("confirm").innerHTML = "<button class=\"rounded-border\" onclick=\"storeConfig()\">Speichern</button>";
   var elem = document.createElement("div");
   elem.innerHTML = "<br/><div class=\"info\"><b><center>Einstellungen gespeichert</center></b></div>";
   document.getElementById("confirm").appendChild(elem);
}

window.doLogin = function()
{
   // console.log("login: " + $("#user").val() + " : " + $.md5($("#password").val()));

   socket.send({ "event": "gettoken", "object":
                 { "user": $("#user").val(),
                   "password": $.md5($("#password").val()) }
               });
}

// ---------------------------------
// gauge stuff ...

function polar_to_cartesian(cx, cy, radius, angle)
{
   var radians = (angle - 90) * Math.PI / 180.0;
   return [Math.round((cx + (radius * Math.cos(radians))) * 100) / 100, Math.round((cy + (radius * Math.sin(radians))) * 100) / 100];
};

function svg_circle_arc_path(x, y, radius, start_angle, end_angle)
{
   var start_xy = polar_to_cartesian(x, y, radius, end_angle);
   var end_xy = polar_to_cartesian(x, y, radius, start_angle);
   return "M " + start_xy[0] + " " + start_xy[1] + " A " + radius + " " + radius + " 0 0 0 " + end_xy[0] + " " + end_xy[1];
};

function initCharts()
{
   console.log("requesting chart data");
   socket.send({ "event": "chartdata", "object": "" });
}

function drawCharts(dataRow, root)
{
   var data = {
      type: "line",
      data: {
         labels: [],
         datasets: []
      },
      options: {
         responsive: true,
         tooltips: {
            mode: "index",
            intersect: false,
         },
         hover: {
            mode: "nearest",
            intersect: true
         },
         scales: {
            xAxes: [{
               type: "time",
               time: { displayFormats: {
                  millisecond: 'MMM DD - HH:MM',
                  second: 'MMM DD - HH:MM',
                  minute: 'HH:MM',
                  hour: 'MMM DD - HH:MM',
                  day: 'HH:MM',
                  week: 'MMM DD - HH:MM',
                  month: 'MMM DD - HH:MM',
                  quarter: 'MMM DD - HH:MM',
                  year: 'MMM DD - HH:MM' } },
               distribution: "linear",
               display: true,
               ticks: {
                  fontColor: "gray"
               },
               gridLines: {
                  color: "gray",
                  borderDash: [5,5]
               },
               scaleLabel: {
                  display: true,
                  labelString: "Zeit"
               }
            }],
            yAxes: [{
               display: true,
               ticks: {
                  fontColor: "gray"
               },
               gridLines: {
                  color: "gray",
                  borderDash: [5,5]
               },
               scaleLabel: {
                  display: true,
                  labelString: "Temperatur [°C]"
               }
            }]
         }
      }
   };

   // console.log("dataRow: " + JSON.stringify(dataRow, undefined, 4));

   data.data.datasets = [];

   var colors = ['blue','green','red','black','purple','yellow'];

   for (var i = 0; i < dataRow.length; i++)
   {
      var dataset = {};

      dataset["data"] = dataRow[i].data;
      dataset["backgroundColor"] = colors[i];
      dataset["borderColor"] = colors[i];
      dataset["label"] = dataRow[i].title;
      dataset["borderWidth"] = 0.7;
      dataset["fill"] = false;
      dataset["pointRadius"] = 0;

      data.data.datasets.push(dataset);
   }

   root.innerHTML = "";
   var chart = new Chart(root.getContext("2d"), data);
}

//----------------------------------------------
// on document ready

$(document).ready(function()
{
   console.log("token: " + localStorage.getItem('token'));
   connectWebSocket();
});


/*
maybe needed for style selection

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
