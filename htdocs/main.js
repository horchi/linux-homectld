
var WebSocketClient = window.WebSocketClient
// import WebSocketClient from "./websocket.js"

const osd2webUrl = "ws://192.168.200.145:4444";

var isActive = null;
var socket = null;
var config = {};
var daemonState = {};
var lastUpdate = "";   // #TODO - set to data age
var documentName = "";
var theChart = null;
var theChartRange = 2;
var theChartStart = new Date(); theChartStart.setDate(theChartStart.getDate()-theChartRange);

var rights = [ "View",
               "Control",
               "Full Control",
               "Settings",
               "Admin" ];    // sync arry with UserRights of poold.h

window.documentReady = function(doc)
{
   daemonState.state = -1;
   documentName = doc;
   console.log("documentReady: " + documentName);

   var url = "ws://" + location.hostname + ":61109";
   var protocol = "pool";

   if (documentName == "vdrfb") {
      protocol = "osd2vdr";
      url = osd2webUrl;

      if (location.hostname.indexOf("192.168.200.145") == -1)
         url = "ws://" + location.hostname + "/poolvdr/ws";   // via apache

      prepareVdrButtons();
   }
   else {
      protocol = "pool";
      if (location.hostname.indexOf("192.168.200.145") == -1)
         url = "ws://" + location.hostname + "/pool/ws";   // via apache
   }

   connectWebSocket(url, protocol);
}

function onSocketConnect(protocol)
{
   var token = localStorage.getItem('token');
   var user = localStorage.getItem('user');

   if (!token || token == null)  token = "";
   if (!user || user == null)    user = "";

   prepareMenu(token != "", protocol == "osd2vdr");

   // request some data at login

   var jsonArray = [];
   var nRequests = 0;
   var jsonRequest = {};

   if (documentName == "syslog")
      jsonRequest["name"] = "syslog";
   else if (documentName == "maincfg")
      jsonRequest["name"] = "configdetails";
   else if (documentName == "usercfg")
      jsonRequest["name"] = "userdetails";
   else if (documentName == "iosetup")
      jsonRequest["name"] = "iosettings";
   else if (documentName == "chart")
      prepareChartRequest(jsonRequest, "", 0, 2)
   else if (documentName == "dashboard")
      jsonRequest["name"] = "data";
   else if (documentName == "list")
      jsonRequest["name"] = "data";

   jsonArray[0] = jsonRequest;

   if (documentName != "login") {
      if (protocol == "osd2vdr")
         socket.send({ "event" : "login", "object" :
                       { "type" : 2,         // ctFB
                         "user" : user,
                         "token" : token,
                         "requests" : jsonArray }
                     });
      else
         socket.send({ "event" : "login", "object" :
                       { "type" : "active",
                         "user" : user,
                         "token" : token,
                         "requests" : jsonArray }
                     });
   }
}

function connectWebSocket(useUrl, protocol)
{
   socket = new WebSocketClient({
      url: useUrl,
      protocol: protocol,
      autoReconnectInterval: 1000,
      onopen: function (){
         console.log("socket opened :)");
         if (isActive === null)     // wurde beim Schliessen auf null gesetzt
            onSocketConnect(protocol);
      }, onclose: function (){
         isActive = null;           // auf null setzen, dass ein neues login aufgerufen wird
      }, onmessage: function (msg) {
         dispatchMessage(msg.data)
      }.bind(this)
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

   if ((event == "update" || event == "all") && rootDashboard) {
      lastUpdate = d.toLocaleTimeString();
      updateDashboard(jMessage.object);
   }
   else if ((event == "update" || event == "all") && rootList) {
      lastUpdate = d.toLocaleTimeString();
      updateList(jMessage.object);
   }
   else if (event == "init" && rootDashboard) {
      lastUpdate = d.toLocaleTimeString();
      initDashboard(jMessage.object, rootDashboard);
      updateDashboard(jMessage.object);
   }
   else if (event == "init" && rootList) {
      lastUpdate = d.toLocaleTimeString();
      initList(jMessage.object, rootList);
      updateList(jMessage.object);
   }
   else if (event == "config") {
      config = jMessage.object;
   }
   else if (event == "configdetails" && rootConfig) {
      initConfig(jMessage.object, rootConfig)
   }
   else if (event == "userdetails" && rootConfig) {
      initUserConfig(jMessage.object, rootConfig)
   }
   else if (event == "daemonstate") {
      daemonState = jMessage.object;
   }
   else if (event == "syslog") {
      showSyslog(jMessage.object);
   }
   else if (event == "token") {
      localStorage.setItem('token', jMessage.object.value);
      localStorage.setItem('user', jMessage.object.user);
      if (jMessage.object.state == "confirm") {
         window.location.replace("index.html");
      } else if (documentName == "login") {
         document.getElementById("confirm").innerHTML = "<div class=\"infoError\"><b><center>Login fehlgeschlagen</center></b></div>";
      }
   }
   else if (event == "valuefacts" && rootIoSetup) {
      initIoSetup(jMessage.object, rootIoSetup);
   }
   else if (event == "chartdata" && rootChart) {
      drawCharts(jMessage.object, rootChart);
   }

   // console.log("event: " + event + " dispatched");
}

function prepareMenu(haveToken, vdr)
{
   var html = "";

   html += "<a href=\"index.html\"><button class=\"rounded-border button1\">Dashboard</button></a>";
   html += "<a href=\"list.html\"><button class=\"rounded-border button1\">Liste</button></a>";
   html += "<a href=\"chart.html\"><button class=\"rounded-border button1\">Charts</button></a>";
   html += "<a href=\"maincfg.html\"><button class=\"rounded-border button1\">Setup</button></a>";
   html += "<button id=\"vdrMenu\" class=\"rounded-border button1\" onclick=\"location.href='vdr.html';\" style=\"visibility:hidden\">VDR</button>";

   html += "<div class=\"menuLogin\">";
   if (haveToken)
      html += "<a href=\"user.html\"><button class=\"rounded-border button1\">[" + localStorage.getItem('user') + "]</button></a>";
   else
      html += "<a href=\"login.html\"><button class=\"rounded-border button1\">Login</button></a>";
   html += "</div>";

   if ($("#navMenu").data("setup") != undefined) {
      html += "<div>";
      html += "  <a href=\"maincfg.html\"><button class=\"rounded-border button2\">Allg. Konfiguration</button></a>";
      html += "  <a href=\"iosetup.html\"><button class=\"rounded-border button2\">IO Setup</button></a>";
      html += "  <a href=\"usercfg.html\"><button class=\"rounded-border button2\">User</button></a>";
      html += "  <a href=\"syslog.html\"><button class=\"rounded-border button2\">Syslog</button></a>";
      html += "</div>";
   }

   if ($("#navMenu").data("iosetup") != undefined) {
      html += "<div id=\"confirm\" class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border button2\" onclick=\"storeIoSetup()\">Speichern</button>";
      html += "</div>";
   }
   else if ($("#navMenu").data("maincfg") != undefined) {
      html += "<div id=\"confirm\" class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border button2\" onclick=\"storeConfig()\">Speichern</button>";
      html += "</div>";
   }
   else if ($("#navMenu").data("maincfg") != undefined) {
      html += "<div id=\"confirm\" class=\"confirmDiv\"/>";
   }

   else if ($("#navMenu").data("login") != undefined) {
      html += "<div id=\"confirm\" class=\"confirmDiv\"/>";
   }

   $("#navMenu").html(html);

   if (vdr && haveToken) {
      document.getElementById("vdrMenu").style.visibility = "visible";
      document.getElementById("vdrMenu").disabled = false;
   }
   else if (haveToken) {
      var url = osd2webUrl;
      if (location.hostname.indexOf("192.168.200.145") == -1)
         url = "ws://" + location.hostname + "/poolvdr/ws";   // via apache

      var s = new WebSocketClient( {
         url: url,
         protocol: "osd2vdr",
         autoReconnectInterval: 0,
         onopen: function (){
            console.log("osd2web socket opened");
            document.getElementById("vdrMenu").style.visibility = "visible";
            document.getElementById("vdrMenu").disabled = false;
            s.ws.close();
         }, onclose: function (){
            console.log("osd2web socket closed");
         }, onmessage: function (msg){
         }
      });
   }
   else {
      document.getElementById("vdrMenu").style.visibility = "hidden";
      document.getElementById("vdrMenu").disabled = true;
   }

   var msg = "DEBUG: Browser: '" + $.browser.name + "' : '" + $.browser.versionNumber + "' : '" + $.browser.version + "'";
   socket.send({ "event" : "logmessage", "object" : { "message" : msg } });
}

function prepareVdrButtons()
{
   var root = document.getElementById("vdrFbContainer");
   var elements = root.getElementsByClassName("vdrButtonDiv");

   for (var i = 0; i < elements.length; i++) {
      elements[i].style.height = getComputedStyle(elements[i]).width;
      if (elements[i].children[0].innerHTML == "")
         elements[i].style.visibility = "hidden";
   }
}

function showSyslog(log)
{
   var root = document.getElementById("syslogContainer");
   root.innerHTML = log.lines.replace(/(?:\r\n|\r|\n)/g, '<br>');
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

   document.getElementById("ioDigitalOut").innerHTML = "";
   document.getElementById("ioOneWire").innerHTML = "";
   document.getElementById("ioOther").innerHTML = "";
   document.getElementById("ioScripts").innerHTML = "";

   for (var i = 0; i < valueFacts.length; i++)
   {
      var item = valueFacts[i];
      var root = null;

      var usrtitle = item.usrtitle != null ? item.usrtitle : "";
      var html = "<td id=\"row_" + item.type + item.address + "\" data-address=\"" + item.address + "\" data-type=\"" + item.type + "\" >" + item.title + "</td>";
      html += "<td class=\"tableMultiColCell\">";
      html += "  <input id=\"usrtitle_" + item.type + item.address + "\" class=\"rounded-border inputSetting\" type=\"text\" value=\"" + usrtitle + "\"/>";
      html += "</td>";
      if (item.type != "DO" && item.type != "SC")
         html += "<td class=\"tableMultiColCell\"><input id=\"scalemax_" + item.type + item.address + "\" class=\"rounded-border inputSetting\" type=\"number\" value=\"" + item.scalemax + "\"/></td>";
      else
         html += "<td class=\"tableMultiColCell\"></td>";
      html += "<td style=\"text-align:center;\">" + item.unit + "</td>";
      html += "<td><input id=\"state_" + item.type + item.address + "\" class=\"rounded-border inputSetting\" type=\"checkbox\" " + (item.state == 1 ? "checked" : "") + " /></td>";
      html += "<td>" + item.type + ":0x" + item.address.toString(16) + "</td>";

      switch (item.type) {
         case 'DO': root = document.getElementById("ioDigitalOut"); break
         case 'W1': root = document.getElementById("ioOneWire");    break
         case 'SP': root = document.getElementById("ioOther");      break
         case 'SC': root = document.getElementById("ioScripts");    break
      }

      if (root != null)
      {
         var elem = document.createElement("tr");
         elem.innerHTML = html;
         root.appendChild(elem);
      }
   }
}

function initUserConfig(users, root)
{
   var table = document.getElementById("userTable");
   table.innerHTML = "";

   for (var i = 0; i < users.length; i++)
   {
      var item = users[i];

      var html = "<td id=\"row_" + item.user + "\" >" + item.user + "</td>";
      html += "<td>";
      for (var b = 0; b < rights.length; b++) {
         html += "<input id=\"bit_" + item.user + b + "\" class=\"rounded-border input\" style=\"width:auto;\" type=\"checkbox\" " + (item.rights & (2 ** b) ? "checked" : "") + "/>"
         html += "<span style=\"padding-right:20px; padding-left:5px;\">" + rights[b] + "</span>";
      }
      html += "</td>";
      html += "<td>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'store')\">Speichern</button>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'resettoken')\">Token zurücksetzen</button>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'resetpwd')\">Passwort zurücksetzen</button>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'delete')\">Löschen</button>";
      html += "</td>";

      var elem = document.createElement("tr");
      elem.innerHTML = html;
      table.appendChild(elem);
   }

   html =  "  <span>User: </span><input id=\"input_user\" class=\"rounded-border input\"/>";
   html += "  <span>Passwort: </span><input id=\"input_passwd\" class=\"rounded-border input\"/>";
   html += "  <button class=\"rounded-border button2\" onclick=\"addUser()\">+</button>";

   document.getElementById("addUserDiv").innerHTML = html;
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
      var id = "id=\"widget" + widget.type + widget.address + "\"";

      if (widget.widgettype == 1 || widget.widgettype == 3) {      // 1 Gauge or 3 Value
         html += "<span class=\"listFirstCol\"" + id + ">" + widget.value.toFixed(2) + "&nbsp;" + widget.unit;
         html += "&nbsp; <p style=\"display:inline;font-size:12px;font-style:italic;\">(" + widget.peak.toFixed(2) + ")</p>";
         html += "</span>";
      }
      else if (widget.widgettype == 0) {   // 0 Symbol
         html += "   <div class=\"listFirstCol\" onclick=\"toggleIo(" + widget.address + ",'" + widget.type + "')\"><img " + id + "/></div>\n";
      }
      else {   // 2 Text
         html += "<span " + id + "></span>";
      }

      html += "<span class=\"listSecondCol listText\" >" + widget.title + "</span>";

      var elem = document.createElement("div");
      elem.className = "listRow";
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

      if (sensor.widgettype == 1 || sensor.widgettype == 3) {
         $(id).html(sensor.value.toFixed(2) + "&nbsp;" + sensor.unit +
                    "&nbsp; <p style=\"display:inline;font-size:12px;font-style:italic;\">(" + sensor.peak.toFixed(2) + ")</p>");
      }
      else if (sensor.widgettype == 0)
         $(id).attr("src", sensor.image);  // $(id).html(sensor.value ? "An" : "Aus");
      else
         $(id).html(sensor.value.toFixed(0));

      // console.log(i + ") " + sensor.widgettype + " : " + sensor.title + " / " + sensor.value + "(" + id + ")");
   }
}

function initDashboard(widgets, root)
{
   if (!widgets) {
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
         elem.setAttribute("onclick", "window.open('chart.html?sensors=" + widget.type + ":0x" + widget.address.toString(16) +
                           "' , '_blank', 'scrollbars=yes,width=1610,height=650,resizable=yes,left=120,top=120')");
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

            var e = document.getElementById("progress" + sensor.type + sensor.address);
            if (e != null)
               e.style.visibility = (sensor.next == null || sensor.next == 0) ? "hidden" : "visible";
            else
               console.log("Element '" + "progress" + sensor.type + sensor.address + "' not found");

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

window.chartSelect = function(action)
{
   console.log("chartSelect clicked for " + action);

   var sensors = "";
   var root = document.getElementById("chartSelector");
   var elements = root.querySelectorAll("[id^='checkChartSel_']");

   for (var i = 0; i < elements.length; i++) {
      if (elements[i].checked) {
         var id = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
         sensors += id + ",";
       }
   }

   theChartRange = parseInt($("#chartRange").val());

   var now = new Date();

   if (action == "next")
      theChartStart.setDate(theChartStart.getDate()+1);
   else if (action == "prev")
      theChartStart.setDate(theChartStart.getDate()-1);
   else if (action == "now")
      theChartStart.setDate(now.getDate()-theChartRange);
   else if (action == "range")
      theChartStart.setDate(now.getDate()-theChartRange);

   // console.log("sensors:  '" + sensors + "'");

   var jsonRequest = {};
   prepareChartRequest(jsonRequest, sensors, theChartStart, theChartRange);

   socket.send({ "event" : "chartdata", "object" : jsonRequest });
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

window.vdrKeyPress = function(key)
{
   // { "event" : "keypress", "object" : { "key" : "menu", "repeat" : 1 } }

   if (key == undefined || key == "")
      return;

   socket.send({ "event": "keypress", "object":
                 { "key": key,
                   "repeat": 1 }
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

window.storeIoSetup = function()
{
   var jsonArray = [];
   var rootSetup = document.getElementById("ioSetupContainer");
   var elements = rootSetup.querySelectorAll("[id^='row_']");

   console.log("storeIoSetup");

   for (var i = 0; i < elements.length; i++) {
      var jsonObj = {};

      var type = $(elements[i]).data("type");
      var address = $(elements[i]).data("address");
      // console.log("  loop for: " + type + ":" + address);
      // console.log("   - usrtitle: " + $("#usrtitle_" + type + address).val());

      jsonObj["type"] = type;
      jsonObj["address"] = address;
      if ($("#scalemax_" + type + address).length)
         jsonObj["scalemax"] = parseInt($("#scalemax_" + type + address).val());
      jsonObj["usrtitle"] = $("#usrtitle_" + type + address).val();
      jsonObj["state"] = $("#state_" + type + address).is(":checked") ? 1 : 0;
      jsonArray[i] = jsonObj;
   }

   socket.send({ "event" : "storeiosetup", "object" : jsonArray });

   // show confirm

   document.getElementById("confirm").innerHTML = "<button class=\"rounded-border\" onclick=\"storeIoSetup()\">Speichern</button>";
   var elem = document.createElement("div");
   elem.innerHTML = "<br/><div class=\"info\"><b><center>Einstellungen gespeichert</center></b></div>";
   document.getElementById("confirm").appendChild(elem);
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

   // show confirm

   document.getElementById("confirm").innerHTML = "<button class=\"rounded-border\" onclick=\"storeConfig()\">Speichern</button>";
   var elem = document.createElement("div");
   elem.innerHTML = "<br/><div class=\"info\"><b><center>Einstellungen gespeichert</center></b></div>";
   document.getElementById("confirm").appendChild(elem);
}

window.userConfig = function(user, action)
{
   console.log("userConfig(" + action + ", " + user + ")");

   if (action == "delete") {
      if (confirm("User '" + user + "' löschen?"))
         socket.send({ "event": "userconfig", "object":
                       { "user": user,
                         "action": "del" }});
   }
   else if (action == "store") {
      var rightsMask = 0;
      for (var b = 0; b < rights.length; b++) {
         if ($("#bit_" + user + b).is(":checked"))
            rightsMask += 2 ** b;
      }

      socket.send({ "event": "userconfig", "object":
                    { "user": user,
                      "action": "store",
                      "rights": rightsMask }});
   }
   else if (action == "resetpwd") {
      var passwd = prompt("neues Passwort", "");
      if (passwd && passwd != "")
         console.log("Reset password to: "+ passwd);
         socket.send({ "event": "userconfig", "object":
                       { "user": user,
                         "passwd": $.md5(passwd),
                         "action": "resetpwd" }});
   }
   else if (action == "resettoken") {
      socket.send({ "event": "userconfig", "object":
                    { "user": user,
                      "action": "resettoken" }});
   }
}

window.chpwd  = function()
{
   var user = localStorage.getItem('user');

   if (user && user != "") {
      console.log("Change password of " + user);

      if ($("#input_passwd").val() != "" && $("#input_passwd").val() == $("#input_passwd2").val()) {
         socket.send({ "event": "changepasswd", "object":
                       { "user": user,
                         "passwd": $.md5($("#input_passwd").val()),
                         "action": "resetpwd" }});
      }
      else
         console.log("Passwords not match or empty");
   }
   else
      console.log("Missing login!");
}

window.addUser = function()
{
   console.log("Add user: " + $("#input_user").val());

   socket.send({ "event": "userconfig", "object":
                 { "user": $("#input_user").val(),
                   "passwd": $.md5($("#input_passwd").val()),
                   "action": "add" }
               });

   $("#input_user").val("");
   $("#input_passwd").val("");
}

window.doLogin = function()
{
   console.log("login: " + $("#user").val() + " : " + $.md5($("#password").val()));

   socket.send({ "event": "gettoken", "object":
                 { "user": $("#user").val(),
                   "password": $.md5($("#password").val()) }
               });
}

window.doLogout = function()
{
   localStorage.removeItem('token');
   localStorage.removeItem('user');
   theUser = "";
   window.location.replace("login.html");
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

// ---------------------------------
// charts

function prepareChartRequest(jRequest, sensors, start, range)
{
   var gaugeSelected = false;

   if (!$.browser.safari || $.browser.versionNumber > 9)
   {
      const urlParams = new URLSearchParams(window.location.search);
      if (urlParams.has("sensors"))
      {
         gaugeSelected = true;
         sensors = urlParams.get("sensors");
      }
   }

   jRequest["name"] = "chartdata";

   console.log("requesting chart for '" + start + "' range " + range);

   if (gaugeSelected) {
      jRequest["sensors"] = sensors;
      jRequest["start"] = 0;   // default (use today-range)
      jRequest["range"] = 1;
   }
   else {
      jRequest["start"] = start == 0 ? 0 : Math.floor(start.getTime()/1000);  // calc unix timestamp
      jRequest["range"] = range;
      jRequest["sensors"] = sensors;
   }
}

function drawCharts(dataObject, root)
{
   if (theChart != null) {
      theChart.destroy();
      theChart = null;
   }

   var data = {
      type: "line",
      data: {
         labels: [],
         datasets: []
      },
      options: {
         responsive: false,
         tooltips: {
            mode: "index",
            intersect: false,
         },
         hover: {
            mode: "nearest",
            intersect: true
         },
         legend: {
            display: true,
            labels: {
               fontColor: "white"
            }
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
                  maxTicksLimit: 25,
                  padding: 10,
                  fontColor: "white"
               },
               gridLines: {
                  color: "gray",
                  borderDash: [5,5]
               },
               scaleLabel: {
                  display: true,
                  fontColor: "white",
                  labelString: "Zeit"
               }
            }],
            yAxes: [{
               display: true,
               ticks: {
                  padding: 10,
                  maxTicksLimit: 20,
                  fontColor: "white"
               },
               gridLines: {
                  color: "gray",
                  zeroLineColor: 'gray',
                  borderDash: [5,5]
               },
               scaleLabel: {
                  display: true,
                  fontColor: "white",
                  labelString: "Temperatur [°C]"
               }
            }]
         }
      }
   };

   // console.log("dataObject: " + JSON.stringify(dataObject, undefined, 4));

   var colors = ['yellow','white','red','lightblue','lightgreen','purple','blue'];

   for (var i = 0; i < dataObject.rows.length; i++)
   {
      var dataset = {};

      dataset["data"] = dataObject.rows[i].data;
      dataset["backgroundColor"] = colors[i];
      dataset["borderColor"] = colors[i];
      dataset["label"] = dataObject.rows[i].title;
      dataset["borderWidth"] = 1.2;
      dataset["fill"] = false;
      dataset["pointRadius"] = 0;

      data.data.datasets.push(dataset);
   }

   var end = new Date();
   end.setDate(theChartStart.getDate()+theChartRange);

   $("#chartTitle").html(theChartStart.toLocaleString('de-DE') + "  -  " + end.toLocaleString('de-DE'));

   $("#chartSelector").html("");

   for (var i = 0; i < dataObject.sensors.length; i++)
   {
      var html = "<div class=\"chartSel\"><input id=\"checkChartSel_" + dataObject.sensors[i].id + "\"type=\"checkbox\" onclick=\"chartSelect('choice')\" " + (dataObject.sensors[i].active ? "checked" : "") + "/>" + dataObject.sensors[i].title + "</div>";
      $("#chartSelector").append(html);
   }

   theChart = new Chart(root.getContext("2d"), data);
}

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
