/*
 *  main.js
 *
 *  (c) 2020-2021 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

var WebSocketClient = window.WebSocketClient

var myProtocol = "poold";
var storagePrefix = "p4d";
var isActive = null;
var socket = null;
var config = {};
var daemonState = {};
var widgetTypes = {};
var valueFacts = {};
var images = [];
var currentPage = "dashboard";
var widgetCharts = {};
var theChart = null;
var theChartRange = 1;
var theChartStart = new Date(); theChartStart.setDate(theChartStart.getDate()-theChartRange);
var chartDialogSensor = "";
var chartBookmarks = {};
var allWidgets = [];

$('document').ready(function() {
   daemonState.state = -1;

   console.log("currentPage: " + currentPage);

   var url = "ws://" + location.hostname + ":" + location.port;
   var protocol = myProtocol;

   connectWebSocket(url, protocol);
});

function onSocketConnect(protocol)
{
   var token = localStorage.getItem(storagePrefix + 'Token');
   var user = localStorage.getItem(storagePrefix + 'User');

   if (token == null) token = "";
   if (user == null)  user = "";

   socket.send({ "event" : "login", "object" :
                 { "type" : "active",
                   "user" : user,
                   "page" : currentPage,
                   "token" : token }
               });

   prepareMenu();
}

function connectWebSocket(useUrl, protocol)
{
   console.log("try socket opened " + protocol);

   socket = new WebSocketClient({
      url: useUrl,
      protocol: protocol,
      autoReconnectInterval: protocol == myProtocol ? 1000 : 0,
      onopen: function (){
         console.log("socket opened " + socket.protocol);
         if (isActive === null)     // wurde beim Schliessen auf null gesetzt
            onSocketConnect(protocol);
      }, onclose: function () {
         isActive = null;           // auf null setzen, dass ein neues login aufgerufen wird
      }, onmessage: function (msg) {
         dispatchMessage(msg.data)
      }.bind(this)
   });

   if (!socket)
      return !($el.innerHTML = "Your Browser will not support Websockets!");
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

var infoDialog = null;

async function showInfoDialog(object, titleMsg, onCloseCallback)
{
   if (infoDialog) {
      infoDialog.dialog('close');
      infoDialog.dialog('destroy').remove();
      infoDialog = null;
   }

   var message = object.message;
   var titleMsg = "";

   if (object.status == -1)
      titleMsg = "Error";
   else if (object.status < -1)
      titleMsg = "Information (" + object.status + ")";
   else if (object.status == 1) {
      var array = message.split("#:#");
      titleMsg = array[0];
      message = array[1];
   }

   var msDuration = 2000;
   var bgColor = "";
   var cls = "no-titlebar";
   var align = "center";

   if (object.status != 0) {
      msDuration = 20000;
      cls = "";
      align = "left";
   }

   var div = document.createElement("div");
   div.style.textAlign = align;
   div.style.whiteSpace = "pre";
   div.style.backgroundColor = bgColor;
   div.className = object.status ? "error-border" : "";
   div.textContent = message;

   $(div).dialog({
      dialogClass: cls,
      width: "60%",
      title: titleMsg,
		modal: true,
      resizable: false,
		closeOnEscape: true,
      minHeight: "0px",
      hide: "fade",
      open: function() {
         infoDialog = $(this);
         setTimeout(function() {
            if (infoDialog)
               infoDialog.dialog('close');
            infoDialog = null }, msDuration);
      },
      close: function() {
         $(this).dialog('destroy').remove();
         infoDialog = null;
      }
   });
}

var progressDialog = null;

async function hideProgressDialog()
{
   if (progressDialog != null) {
      console.log("hide progress");
      progressDialog.dialog('destroy').remove();
      progressDialog = null;
   }
}

async function showProgressDialog()
{
   hideProgressDialog();

   var msDuration = 30000;   // timeout 30 seconds
   var form = document.createElement("form");
   form.style.overflow = "hidden";
   var div = document.createElement("div");
   form.appendChild(div);
   div.className = "progress";

   console.log("show progress");

   $(form).dialog({
      dialogClass: "no-titlebar rounded-border",
      width: "125px",
      title: "",
		modal: true,
      resizable: false,
		closeOnEscape: false,
      minHeight: "0px",
      hide: "fade",
      open: function() {
         progressDialog = $(this); setTimeout(function() {
            if (progressDialog)
               progressDialog.dialog('close');
            progressDialog = null }, msDuration);
      },
      close: function() {
         $(this).dialog('destroy').remove();
         progressDialog = null;
      }
   });
}

function dispatchMessage(message)
{
   var jMessage = JSON.parse(message);
   var event = jMessage.event;

   if (event != "chartdata")
      console.log("got event: " + event);

   if (event == "result") {
      hideProgressDialog();
      showInfoDialog(jMessage.object);
   }
   else if ((event == "update" || event == "all")) {
      if (event == "all")
         allWidgets = jMessage.object;
      else {
         for (var i = 0; i < jMessage.object.length; i++) {
            for (var w = 0; w < allWidgets.length; w++) {
               if (jMessage.object[i].name == allWidgets[w].name) {
                  //console.log("update '" + allWidgets[w].name);
                  //console.log("update '" + allWidgets[w].name + "' to (" + jMessage.object[i].value.toFixed(2) + ") from (" + allWidgets[w].value.toFixed(2) + ")");
                  allWidgets[w] = jMessage.object[i];
               }
            }
         }
      }
      if (currentPage == 'dashboard')
         updateDashboard(jMessage.object, event == "all");
      else if (currentPage == 'list')
         updateList(jMessage.object);
   }
   else if (event == "init" && currentPage == 'dashboard') {
      allWidgets = jMessage.object;

      if (currentPage == 'dashboard')
         initDashboard();
      else if (currentPage == 'list')
         initList();
   }
   else if (event == "chartbookmarks") {
      chartBookmarks = jMessage.object;
      updateChartBookmarks();
   }
   else if (event == "config") {
      config = jMessage.object;
   }
   else if (event == "configdetails" && currentPage == 'setup') {
      initConfig(jMessage.object)
   }
   else if (event == "userdetails" && currentPage == 'userdetails') {
      initUserConfig(jMessage.object);
   }
   else if (event == "daemonstate") {
      daemonState = jMessage.object;
   }
   else if (event == "widgettypes") {
      widgetTypes = jMessage.object;
   }
   else if (event == "syslog") {
      showSyslog(jMessage.object);
   }
   else if (event == "token") {
      localStorage.setItem(storagePrefix + 'Token', jMessage.object.value);
      localStorage.setItem(storagePrefix + 'User', jMessage.object.user);
      localStorage.setItem(storagePrefix + 'Rights', jMessage.object.rights);

      if (jMessage.object.state == "confirm") {
         window.location.replace("index.html");
      } else {
         if (document.getElementById("confirm"))
            document.getElementById("confirm").innerHTML = "<div class=\"infoError\"><b><center>Login fehlgeschlagen</center></b></div>";
      }
   }
   else if (event == "valuefactsios" && currentPage == 'iosetup') {
      initIoSetup(valueFacts);
   }
   else if (event == "valuefacts") {
      valueFacts = jMessage.object;
   }
   else if (event == "images") {
      images = jMessage.object;
   }
   else if (event == "chartdata") {
      hideProgressDialog();
      var id = jMessage.object.id;

      if (currentPage == 'chart') {                                 // the charts page
         drawCharts(jMessage.object);
      }
      else if (currentPage == 'dashboard' && id == "chartwidget") { // the dashboard widget
         drawChartWidget(jMessage.object);
      }
      else if (currentPage == 'iosetup' && id == "chartwidget") { // the dashboard widget
         drawChartWidget(jMessage.object);
      }

      else if (currentPage == 'dashboard' && id == "chartdialog") { // the dashboard chart dialog
         drawChartDialog(jMessage.object);
      }
   }

   // console.log("event: " + event + " dispatched");
}

function prepareMenu()
{
   var html = "";
   var haveToken = localStorage.getItem(storagePrefix + 'Token') && localStorage.getItem(storagePrefix + 'Token') != "";

   console.log("prepareMenu: " + currentPage);

   // html += "<a href=\"index.html\"><button class=\"rounded-border button1\">Dashboard</button></a>";
   html += '<button class="rounded-border button1" onclick="mainMenuSel(\'dashboard\')">Dashboard</button>';
   html += '<button class="rounded-border button1" onclick="mainMenuSel(\'list\')">Liste</button>';
   html += '<button class="rounded-border button1" onclick="mainMenuSel(\'chart\')">Charts</button>';
   html += '<button id="vdrMenu" class="rounded-border button1" onclick="mainMenuSel(\'vdr\')">VDR</button>';

   html += "<div class=\"menuLogin\">";
   if (haveToken)
      html += '<button class="rounded-border button1" onclick="mainMenuSel(\'user\')">[' + localStorage.getItem(storagePrefix + 'User') + ']</button>';
   else
      html += '<button class="rounded-border button1" onclick="mainMenuSel(\'login\')">Login</button>';
   html += "</div>";

   if (localStorage.getItem(storagePrefix + 'Rights') & 0x08 || localStorage.getItem(storagePrefix + 'Rights') & 0x10) {
      html += '<button class="rounded-border button1" onclick="mainMenuSel(\'setup\')">Setup</button>';

      if (currentPage == "setup" || currentPage == "iosetup" || currentPage == "userdetails" || currentPage == "syslog") {
         html += "<div>";
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'setup\')">Allg. Konfiguration</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'iosetup\')">IO Setup</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'userdetails\')">User</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'syslog\')">Syslog</button>';
         html += "</div>";
      }
   }

   // buttons below menu

   if (currentPage == "iosetup") {
      html += "<div class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"storeIoSetup()\">Speichern</button>";
      html += "  <button class=\"rounded-border buttonOptions\" id=\"filterIoSetup\" onclick=\"filterIoSetup()\">[alle]</button>";
      html += "</div>";
   }
   else if (currentPage == "setup") {
      html += "<div class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"storeConfig()\">Speichern</button>";
      html += "  <button class=\"rounded-border buttonOptions\" title=\"Letzter Reset: " + config.peakResetAt + "\" id=\"buttonResPeaks\" onclick=\"resetPeaks()\">Reset Peaks</button>";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"sendMail('Test Mail', 'test')\">Test Mail</button>";
      html += "</div>";
   }
   else if (currentPage == "login") {
      html += '<div id="confirm" class="confirmDiv"/>';
   }

   $("#navMenu").html(html);

/*   if (config.vdr == 1 && currentPage == "vdr") {
      if (haveToken) {
         var url = "ws://" + location.hostname + ":4444";

         console.log("osd2web try to open: " + url);

         var s = new WebSocketClient( {
            url: url,
            protocol: "osd2vdr",
            autoReconnectInterval: 0,
            onopen: function (){
               document.getElementById("vdrMenu").style.visibility = "visible";
               document.getElementById("vdrMenu").style.width = "auto";
               document.getElementById("vdrMenu").disabled = false;
               s.ws.close();
            }, onclose: function (){
               // console.log("osd2web socket closed");
            }, onmessage: function (msg){
            }
         });
      }
      else {
         document.getElementById("vdrMenu").style.visibility = "hidden";
         document.getElementById("vdrMenu").style.width = "0px";
         document.getElementById("vdrMenu").disabled = true;
      }
   }*/
}

function mainMenuSel(what)
{
   var lastPage = currentPage;
   currentPage = what;
   console.log("switch to " + currentPage);

   hideAllContainer();

   if (currentPage != lastPage && (currentPage == "vdr" || lastPage == "vdr")) {
      console.log("closing socket " + socket.protocol);
      socket.close();
      // delete socket;
      socket = null;

      var protocol = myProtocol;
      var url = "ws://" + location.hostname + ":" + location.port;

      if (currentPage == "vdr") {
         protocol = "osd2vdr";
         url = "ws://" + location.hostname + ":4444";
      }

      connectWebSocket(url, protocol);
   }

   prepareMenu();

   if (currentPage != "vdr")
      socket.send({ "event" : "pagechange", "object" : { "page"  : currentPage }});

   var event = null;
   var jsonRequest = {};

   if (currentPage == "setup")
      event = "setup";
   else if (currentPage == "iosetup")
      initIoSetup(valueFacts);
   else if (currentPage == "user")
      initUser();
   else if (currentPage == "userdetails")
      event = "userdetails";
   else if (currentPage == "syslog")
      event = "syslog";
   else if (currentPage == "list")
      initList();
   else if (currentPage == "dashboard")
      initDashboard();
   else if (currentPage == "vdr")
      initVdr();
   else if (currentPage == "chart") {
      event = "chartdata"
      prepareChartRequest(jsonRequest, "", theChartStart, theChartRange, "chart");
   }

   if (currentPage != "vdr" && currentPage != "iosetup") {
      if (jsonRequest != {})
         socket.send({ "event" : event, "object" : jsonRequest });
      else if (event != null)
         socket.send({ "event" : event, "object" : { } });

      if (currentPage == 'login')
         initLogin();
   }
}

function initLogin()
{
   $('#container').removeClass('hidden');

   document.getElementById("container").innerHTML =
      '<div id="loginContainer" class="rounded-border inputTableConfig">' +
      '  <table>' +
      '    <tr>' +
      '      <td>User:</td>' +
      '      <td><input id="user" class="rounded-border input" type="text" value=""></input></td>' +
      '    </tr>' +
      '    <tr>' +
      '      <td>Passwort:</td>' +
      '      <td><input id="password" class="rounded-border input" type="password" value=""></input></td>' +
      '    </tr>' +
      '  </table>' +
      '  <button class="rounded-border buttonHighlighted" onclick="doLogin()">Anmelden</button>' +
      '</div>';
}

function showSyslog(log)
{
   $('#container').removeClass('hidden');
   document.getElementById("container").innerHTML = '<div id="syslogContainer" class="log"></div>';

   var root = document.getElementById("syslogContainer");
   root.innerHTML = log.lines.replace(/(?:\r\n|\r|\n)/g, '<br>');
}

window.sendMail = function(subject, body)
{
   socket.send({ "event" : "sendmail", "object" : { "subject" : subject, "body" : body } });
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

function doLogin()
{
   // console.log("login: " + $("#user").val() + " : " + $.md5($("#password").val()));
   socket.send({ "event": "gettoken", "object":
                 { "user": $("#user").val(),
                   "password": $.md5($("#password").val()) }
               });
}

function doLogout()
{
   localStorage.removeItem(storagePrefix + 'Token');
   localStorage.removeItem(storagePrefix + 'User');
   localStorage.removeItem(storagePrefix + 'Rights');
   console.log("logout");
   mainMenuSel('login');
}

function hideAllContainer()
{
   $('#stateContainer').addClass('hidden');
   $('#container').addClass('hidden');
}

// ---------------------------------
// charts

function prepareChartRequest(jRequest, sensors, start, range, id)
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

   jRequest["id"] = id;
   jRequest["name"] = "chartdata";

   // console.log("requesting chart for '" + start + "' range " + range);

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

function drawChartWidget(dataObject)
{
   var root = document.getElementById("container");
   var id = "widget" + dataObject.rows[0].sensor;

   if (widgetCharts[id] != null) {
      widgetCharts[id].destroy();
      widgetCharts[id] = null;
   }

   var data = {
      type: "line",
      data: {
         datasets: []
      },
      options: {
         legend: {
            display: false
         },
         responsive: true,
         maintainAspectRatio: false,
         aspectRatio: false,
         scales: {
            xAxes: [{
               type: "time",
               distribution: "linear",
               display: false
            }],
            yAxes: [{
               display: false
            }]
         }
      }
   };

   for (var i = 0; i < dataObject.rows.length; i++)
   {
      var dataset = {};

      dataset["data"] = dataObject.rows[i].data;
      dataset["backgroundColor"] = "#415969";  // fill color
      dataset["borderColor"] = "#3498db";      // line color
      dataset["fill"] = true;
      dataset["pointRadius"] = 2.0;
      data.data.datasets.push(dataset);
   }

   var canvas = document.getElementById(id);
   widgetCharts[id] = new Chart(canvas, data);
}

function drawChartDialog(dataObject)
{
   var root = document.querySelector('dialog')
   if (dataObject.rows[0].sensor != chartDialogSensor) {
      return ;
   }

   if (theChart != null) {
      theChart.destroy();
      theChart = null;
   }

   var data = {
      type: "line",
      data: {
         datasets: []
      },
      options: {
         legend: {
            display: true
         },
         responsive: true,
         maintainAspectRatio: false,
         aspectRatio: false,
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

   for (var i = 0; i < dataObject.rows.length; i++)
   {
      var dataset = {};

      dataset["data"] = dataObject.rows[i].data;
      dataset["backgroundColor"] = "#415969";  // fill color
      dataset["borderColor"] = "#3498db";      // line color
      dataset["fill"] = true;
      dataset["label"] = dataObject.rows[i].title;
      dataset["pointRadius"] = 0;
      data.data.datasets.push(dataset);
   }

   var canvas = root.querySelector("#chartDialog");
   theChart = new Chart(canvas, data);
}
